// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "umd/device/utils/error.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace tt::umd {

/**
 * Returns the per-op MMIO budget (env-driven: TT_UMD_MMIO_OP_TIMEOUT_MS, default 100 ms).
 *
 * Defined out-of-line in device_memcpy.cpp so the getenv parsing stays in one TU; the
 * templated memcpy primitives below call it once per invocation (in the MmioOpTimer ctor).
 */
std::chrono::milliseconds mmio_op_timeout();

/**
 * Default per-op timeout policy used when a caller supplies no callback: every
 * overrun aborts (throws tt::umd::error::DeviceTimeoutError). operator() returning
 * true means "confirm the stall / abort"; see the callback contract below.
 */
struct AbortOnTimeout {
    bool operator()() const noexcept { return true; }
};

/**
 * Timeout callback contract (the TimeoutFn template parameter below).
 *
 * The callback is invoked only when an MMIO op exceeds the per-op budget.
 * Return `true` to abort the memcpy (throws tt::umd::error::DeviceTimeoutError).
 * Return `false` to treat the slow op as a false positive — the memcpy
 * continues with the next op getting a fresh budget.
 *
 * Typical use is a NOC hang check: probe the device cheaply and return
 * "is this op stalled because the device is actually hung?".
 *
 * Passing no callback (the AbortOnTimeout default) makes every overrun abort.
 *
 * Contract: the callback must issue any device I/O it performs through a path
 * that does NOT re-enter a timed memcpy and does NOT re-take a lock already held
 * by the stalled op. There is no re-entrancy guard inside memcpy — a callback
 * that reads back through a timed path (with a timeout callback of its own) would
 * recurse, and one that re-takes the caller's I/O lock would deadlock. A cheap
 * BAR-based probe (e.g. HangDetector::is_pcie_hung) satisfies this; routing the
 * probe through the locked, TLB-mapped block path (e.g. is_noc_hung via
 * PcieProtocol::read_from_device) does not.
 *
 * The callback type is a template parameter (not std::function) so a concrete
 * callable is inlined and incurs no heap allocation. Callers that need runtime
 * type erasure (e.g. a std::function stored in a polymorphic class) should wrap
 * it in a thin adapter functor and pass that.
 */

namespace detail {

// Per-call helper shared by all device_memcpy primitives. Reads the env-driven budget
// once in the ctor and checks each op's delta against it. `remaining` is held by
// reference so the thrown error message reflects how many bytes were left when the
// stall fired. Templated on the callback type so the (cold-path) on_timeout call is
// a direct, inlinable call rather than a std::function dispatch.
template <typename TimeoutFn>
class MmioOpTimer {
public:
    MmioOpTimer(
        const char* fn_name,
        const char* op_verb,
        std::size_t total_size,
        const std::size_t& remaining,
        const TimeoutFn& on_timeout) :
        fn_name_(fn_name),
        op_verb_(op_verb),
        total_size_(total_size),
        remaining_(remaining),
        on_timeout_(on_timeout),
        op_timeout_(mmio_op_timeout()) {}

    void record_and_check(std::chrono::steady_clock::time_point t_before, std::uint32_t op_bytes) {
        auto t_now = std::chrono::steady_clock::now();
        auto delta = t_now - t_before;
        if (delta < op_timeout_) {
            return;
        }
        // Over budget. The callback gets to confirm the stall: returning false (false
        // positive, device healthy) resumes the memcpy with a fresh budget for the next op;
        // returning true aborts. The AbortOnTimeout default returns true, so with no real
        // callback the overrun aborts outright and the budget protects every caller. There
        // is no re-entrancy guard: the callback must issue any device I/O it performs through
        // a path that does NOT re-enter a timed memcpy (see the contract above), else recurse.
        if (!on_timeout_()) {
            return;
        }
        throw error::DeviceTimeoutError(
            fn_name_,
            op_verb_,
            op_bytes,
            std::chrono::duration_cast<std::chrono::nanoseconds>(delta),
            op_timeout_,
            remaining_,
            total_size_);
    }

private:
    const char* fn_name_;
    const char* op_verb_;
    std::size_t total_size_;
    const std::size_t& remaining_;
    const TimeoutFn& on_timeout_;
    std::chrono::milliseconds op_timeout_;
};

}  // namespace detail

// The single-word scalar transfers below get the same per-op budget as the bulk memcpy paths:
// the volatile store/load is bracketed by a steady_clock sample and checked via MmioOpTimer, so a
// slow-but-completing op (e.g. a ~700 ms read of a hung NOC register) trips the timeout instead of
// silently returning. A hard stall inside the instruction is still caught by SIGBUS, not here.
template <typename TimeoutFn = AbortOnTimeout>
void write16_to_device(volatile void* dest, std::uint16_t value, const TimeoutFn& on_timeout = {}) {
    std::size_t remaining = sizeof(value);
    detail::MmioOpTimer<TimeoutFn> timer("write16_to_device", "store", sizeof(value), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    *reinterpret_cast<volatile std::uint16_t*>(dest) = value;
    remaining = 0;
    timer.record_and_check(t, sizeof(value));
}

template <typename TimeoutFn = AbortOnTimeout>
void write32_to_device(volatile void* dest, std::uint32_t value, const TimeoutFn& on_timeout = {}) {
    std::size_t remaining = sizeof(value);
    detail::MmioOpTimer<TimeoutFn> timer("write32_to_device", "store", sizeof(value), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    *reinterpret_cast<volatile std::uint32_t*>(dest) = value;
    remaining = 0;
    timer.record_and_check(t, sizeof(value));
}

template <typename TimeoutFn = AbortOnTimeout>
std::uint16_t read16_from_device(const volatile void* src, const TimeoutFn& on_timeout = {}) {
    std::size_t remaining = sizeof(std::uint16_t);
    detail::MmioOpTimer<TimeoutFn> timer("read16_from_device", "load", sizeof(std::uint16_t), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    std::uint16_t value = *reinterpret_cast<const volatile std::uint16_t*>(src);
    remaining = 0;
    timer.record_and_check(t, sizeof(value));
    return value;
}

template <typename TimeoutFn = AbortOnTimeout>
std::uint32_t read32_from_device(const volatile void* src, const TimeoutFn& on_timeout = {}) {
    std::size_t remaining = sizeof(std::uint32_t);
    detail::MmioOpTimer<TimeoutFn> timer("read32_from_device", "load", sizeof(std::uint32_t), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    std::uint32_t value = *reinterpret_cast<const volatile std::uint32_t*>(src);
    remaining = 0;
    timer.record_and_check(t, sizeof(value));
    return value;
}

/**
 * memcpy for writes targeting device memory mapped through a TLB window.
 *
 * Standard memcpy (glibc) can emit overlapping stores to the same address, which causes
 * double writes when the destination is device memory. This routine issues each store
 * explicitly so every destination address is written exactly once.
 *
 * On x86_64: bulk transfers use AVX2 unaligned loads/stores (VMOVDQU 256-bit), with
 * 16-byte (SSE), 4-byte and byte-wide tails.
 *
 * On other architectures: falls back to explicit 4-byte and byte-wide volatile stores.
 *
 * Handles arbitrary alignment and size — leading/trailing bytes smaller than a DWORD are
 * written as individual byte-wide PCIe transactions (the Blackhole PCIe controller supports
 * sub-DWORD writes natively, so no read-modify-write is required).
 *
 * Per-op budget: TLB-touching stores must complete within a hard-coded budget
 * (default 100 ms, overridable at process start via TT_UMD_MMIO_OP_TIMEOUT_MS).
 * The check is applied once per 256-byte block in the bulk AVX2 phase, and once
 * per op in the 32 / 16 / 4-byte and byte-wide tail phases.
 * On overrun:
 *   - with the AbortOnTimeout default (no callback), throws tt::umd::error::DeviceTimeoutError;
 *   - if on_timeout returns true, throws DeviceTimeoutError;
 *   - if on_timeout returns false, the memcpy continues with the next op getting a fresh budget.
 */
template <typename TimeoutFn = AbortOnTimeout>
void memcpy_to_device(volatile void* dest, const void* src, std::size_t size, const TimeoutFn& on_timeout = {}) {
    const std::size_t original_size = size;
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    detail::MmioOpTimer<TimeoutFn> timer("memcpy_to_device", "store", original_size, size, on_timeout);

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        timer.record_and_check(t, 1);
    }

#if defined(__x86_64__) || defined(_M_X64)
    // The AVX2 intrinsics below are not themselves volatile. Each record_and_check() call
    // is an opaque function call and therefore acts as a compiler barrier — it prevents
    // the compiler from reordering the volatile-conceptual SIMD store around it. The
    // bulk loop checks the timeout once per 256-byte block (after all 8 unrolled AVX2
    // stores), keeping the store pipeline intact within a block. The explicit volatile
    // loops in Phases 0/4/5 still bound the ordering for the byte/word tails.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* d_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(d));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned stores).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        // Loads are from host memory (s) — no TLB access, not instrumented.
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 224));

        // All 8 stores go to TLB-mapped device memory. One timeout check after the
        // whole 256-byte block keeps the AVX2 store pipeline intact while still
        // detecting any stall — a single posted-write pile-up will stall the CPU
        // mid-block and the check fires once the block completes.
        auto t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 32), v1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 64), v2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 96), v3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 128), v4);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 160), v5);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 192), v6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 224), v7);
        timer.record_and_check(t, 256);

        d_simd += 256;
        s += 256;
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        auto t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v);
        timer.record_and_check(t, 32);
        d_simd += 32;
        s += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        auto t = std::chrono::steady_clock::now();
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d_simd), v);
        timer.record_and_check(t, 16);
        d_simd += 16;
        s += 16;
        size -= 16;
    }

    d = reinterpret_cast<volatile std::uint8_t*>(d_simd);
#endif

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        auto t = std::chrono::steady_clock::now();
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        timer.record_and_check(t, 4);
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        timer.record_and_check(t, 1);
    }
}

/**
 * memcpy for reads from device memory mapped through a TLB window.
 *
 * On x86_64: bulk transfers use AVX2 unaligned loads/stores (VMOVDQU 256-bit), with
 * 16-byte (SSE), 4-byte and byte-wide tails.
 *
 * On other architectures: falls back to explicit 4-byte and byte-wide volatile loads.
 *
 * Per-op budget semantics match memcpy_to_device. The check is placed once per 256-byte
 * AVX2 block (and once per op in the tails), so the 8 non-posted PCIe reads within a block
 * still pipeline; the timeout boundary falls between blocks rather than between loads.
 */
template <typename TimeoutFn = AbortOnTimeout>
void memcpy_from_device(void* dest, const volatile void* src, std::size_t size, const TimeoutFn& on_timeout = {}) {
    const std::size_t original_size = size;
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    detail::MmioOpTimer<TimeoutFn> timer("memcpy_from_device", "load", original_size, size, on_timeout);

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        timer.record_and_check(t, 1);
    }

#if defined(__x86_64__) || defined(_M_X64)
    // See memcpy_to_device for the volatile-ordering / compiler-barrier reasoning.
    // The bulk loop checks the timeout once per 256-byte block (after all 8 unrolled
    // AVX2 loads), so the CPU can keep 8 non-posted PCIe reads in flight concurrently
    // within a block. Coarser checks in the tail phases (1 per 32/16/4/byte op) keep
    // detection responsive even at sub-256-byte granularity.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned loads).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        // All 8 loads come from TLB-mapped device memory. One timeout check after the
        // whole 256-byte block restores the 8-way non-posted-read pipeline (the CPU can
        // have up to 8 reads outstanding concurrently) while still detecting any stall.
        auto t = std::chrono::steady_clock::now();
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 224));
        timer.record_and_check(t, 256);

        // Stores go to host memory (d) — no TLB access, not instrumented.
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 32), v1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 64), v2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 96), v3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 128), v4);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 160), v5);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 192), v6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 224), v7);

        d += 256;
        s_simd += 256;
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        auto t = std::chrono::steady_clock::now();
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        timer.record_and_check(t, 32);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v);
        d += 32;
        s_simd += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        auto t = std::chrono::steady_clock::now();
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s_simd));
        timer.record_and_check(t, 16);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        d += 16;
        s_simd += 16;
        size -= 16;
    }

    s = reinterpret_cast<const volatile std::uint8_t*>(s_simd);
#endif

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        auto t = std::chrono::steady_clock::now();
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        timer.record_and_check(t, 4);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile loads.
    while (size > 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        timer.record_and_check(t, 1);
    }
}

}  // namespace tt::umd
