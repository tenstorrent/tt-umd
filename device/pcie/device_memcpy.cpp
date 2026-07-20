// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device_memcpy.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <numeric>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/utils/error.hpp"
#include "umd/device/utils/mmio_timeout_config.hpp"
#include "umd/device/utils/op_timeout_guard.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace tt::umd {

namespace {

// Builds the per-op timeout guard for the MMIO transfer routines: the configured budget, the on_timeout
// callback, and an overrun action that throws DeviceTimeoutError. `bytes_remaining` is captured by
// reference so the error reflects how much was left when the stall fired. on_timeout must not re-enter
// the stalled op (see the on_timeout docs in the header).
auto make_mmio_timeout_guard(
    const char* op_verb,
    std::size_t total_bytes,
    const std::size_t& bytes_remaining,
    const std::function<bool()>& on_timeout) {
    const auto budget = MmioTimeoutConfig::get_op_timeout();
    return OpTimeoutGuard(
        budget,
        on_timeout,
        [op_verb, budget, total_bytes, &bytes_remaining](std::chrono::nanoseconds delta, std::uint32_t op_bytes) {
            UMD_THROW(error::DeviceTimeoutError, op_verb, op_bytes, delta, budget, bytes_remaining, total_bytes);
        });
}

// Opt-in per-op MMIO timing instrumentation, toggled by TT_UMD_MEMCPY_TIMING.
const bool MEMCPY_TIMING_ENABLED = std::getenv("TT_UMD_MEMCPY_TIMING") != nullptr;

struct MemcpyOpTiming {
    std::int64_t ns;
    std::uint32_t bytes;
};

// Reused across calls on the same thread so steady-state recording never allocates. A single shared
// buffer is safe because the two instrumented entry points (memcpy_to_device / memcpy_from_device) never nest on one
// thread.
thread_local std::vector<MemcpyOpTiming> g_memcpy_op_timings;

// Reused across calls on the same thread so dump() reallocates at most once (after warmup) instead of
// building a fresh string every call. Same non-nesting safety argument as g_memcpy_op_timings.
thread_local std::string g_memcpy_dump_buffer;

// RAII per-call timing recorder. Clears the thread-local buffer on construction, appends one entry per
// instrumented op via record(), and dumps the aggregate summary on destruction. Dumping from the
// destructor means a transfer that aborts via a timeout throw still reports its ops, including the
// stalling one.
class MemcpyTimingRecorder {
public:
    MemcpyTimingRecorder(const char* fn_name, std::size_t total_size) : fn_name_(fn_name), total_size_(total_size) {
        if (MEMCPY_TIMING_ENABLED) {
            g_memcpy_op_timings.clear();
        }
    }

    MemcpyTimingRecorder(const MemcpyTimingRecorder&) = delete;
    MemcpyTimingRecorder& operator=(const MemcpyTimingRecorder&) = delete;

    ~MemcpyTimingRecorder() noexcept {
        if (!MEMCPY_TIMING_ENABLED) {
            return;
        }

        try {
            dump();
        } catch (const std::exception& e) {
            log_warning(LogUMD, "MemcpyTimingRecorder::dump() failed, timing summary dropped: {}", e.what());
        } catch (...) {
            log_warning(LogUMD, "MemcpyTimingRecorder::dump() failed, timing summary dropped");
        }
    }

    void record(std::chrono::steady_clock::time_point start, std::uint32_t bytes) noexcept {
        if (!MEMCPY_TIMING_ENABLED) {
            return;
        }
        const auto delta =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
        try {
            g_memcpy_op_timings.push_back({delta.count(), bytes});
        } catch (...) {
            if (!record_warn_emitted_) {
                record_warn_emitted_ = true;
                log_warning(LogUMD, "MemcpyTimingRecorder::record() dropped op timing on allocation failure");
            }
        }
    }

private:
    // Emits one line to stderr: function name, total size, op count, min/max/mean/total ns, and the full
    // per-op ns:bytes list in recording order.
    void dump() const {
        if (g_memcpy_op_timings.empty()) {
            return;
        }

        const auto [min_it, max_it] = std::minmax_element(
            g_memcpy_op_timings.begin(),
            g_memcpy_op_timings.end(),
            [](const MemcpyOpTiming& a, const MemcpyOpTiming& b) { return a.ns < b.ns; });
        const std::int64_t min_ns = min_it->ns;
        const std::int64_t max_ns = max_it->ns;
        const std::int64_t total_ns = std::accumulate(
            g_memcpy_op_timings.begin(),
            g_memcpy_op_timings.end(),
            std::int64_t{0},
            [](std::int64_t acc, const MemcpyOpTiming& op) { return acc + op.ns; });
        const std::size_t count = g_memcpy_op_timings.size();
        const std::int64_t mean_ns = total_ns / static_cast<std::int64_t>(count);

        // Build into the reused thread-local buffer with std::to_chars.
        std::string& out = g_memcpy_dump_buffer;
        out.clear();
        // Reserve up front to avoid reallocations while appending. 96 bytes covers the fixed
        // header (labels + the handful of summary integers); each op contributes an "ns:bytes,"
        // entry, budgeted at ~12 bytes. These are rough over-estimates: a short reserve only
        // costs a later reallocation, so exactness does not matter.
        out.reserve(96 + count * 12);
        // Scratch for std::to_chars. A 64-bit integer is at most 20 digits, plus a sign; 24
        // rounds that up with margin so no formatted value can overflow the buffer.
        char num[24];
        auto append_int = [&out, &num](auto value) {
            const auto result = std::to_chars(num, num + sizeof(num), value);
            out.append(num, result.ptr - num);
        };
        out += '[';
        out += fn_name_;
        out += "] size=";
        append_int(total_size_);
        out += " ops=";
        append_int(count);
        out += " min=";
        append_int(min_ns);
        out += "ns max=";
        append_int(max_ns);
        out += "ns mean=";
        append_int(mean_ns);
        out += "ns total=";
        append_int(total_ns);
        out += "ns ops_ns:bytes=";
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                out += ',';
            }
            append_int(g_memcpy_op_timings[i].ns);
            out += ':';
            append_int(g_memcpy_op_timings[i].bytes);
        }
        out += '\n';
        std::fwrite(out.data(), 1, out.size(), stderr);
    }

    const char* fn_name_;
    std::size_t total_size_;
    bool record_warn_emitted_ = false;
};

}  // namespace

// The single-word scalar transfers below get the same per-op budget as the bulk memcpy paths:
// the volatile store/load is bracketed by a steady_clock sample and checked via the timeout guard, so
// a slow-but-completing op (e.g. a ~700 ms read of a hung NOC register) trips the timeout instead of
// silently returning. The check brackets each op, so it bounds a slow-but-completing access but cannot
// preempt a CPU already stalled mid-access; SIGBUS covers only mapping invalidation (e.g. on reset),
// not a stall.
void write16_to_device(volatile void* dest, std::uint16_t value, const std::function<bool()>& on_timeout) {
    std::size_t remaining = 0;
    auto timer = make_mmio_timeout_guard("store", sizeof(value), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    *reinterpret_cast<volatile std::uint16_t*>(dest) = value;
    timer.record_and_check(t, sizeof(value));
}

void write32_to_device(volatile void* dest, std::uint32_t value, const std::function<bool()>& on_timeout) {
    std::size_t remaining = 0;
    auto timer = make_mmio_timeout_guard("store", sizeof(value), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    *reinterpret_cast<volatile std::uint32_t*>(dest) = value;
    timer.record_and_check(t, sizeof(value));
}

std::uint16_t read16_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    std::size_t remaining = 0;
    auto timer = make_mmio_timeout_guard("load", sizeof(std::uint16_t), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    std::uint16_t value = *reinterpret_cast<const volatile std::uint16_t*>(src);
    timer.record_and_check(t, sizeof(value));
    return value;
}

std::uint32_t read32_from_device(const volatile void* src, const std::function<bool()>& on_timeout) {
    std::size_t remaining = 0;
    auto timer = make_mmio_timeout_guard("load", sizeof(std::uint32_t), remaining, on_timeout);
    auto t = std::chrono::steady_clock::now();
    std::uint32_t value = *reinterpret_cast<const volatile std::uint32_t*>(src);
    timer.record_and_check(t, sizeof(value));
    return value;
}

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size, const std::function<bool()>& on_timeout) {
    const std::size_t original_size = size;
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    auto timer = make_mmio_timeout_guard("store", original_size, size, on_timeout);
    MemcpyTimingRecorder recorder("memcpy_to_device", original_size);

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        recorder.record(t, 1);
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
        recorder.record(t, 256);
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
        recorder.record(t, 32);
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
        recorder.record(t, 16);
        timer.record_and_check(t, 16);
        d_simd += 16;
        s += 16;
        size -= 16;
    }

    d = reinterpret_cast<volatile std::uint8_t*>(d_simd);

#elif defined(__GNUC__)
    // Like the x86 path, these wide stores are not volatile — compiler reordering is
    // bounded by the Phase 0/4/5 volatile loops. On Clang, __builtin_nontemporal_store
    // lowers to STNP (weakly ordered on AArch64), so a release fence is inserted after
    // Phases 1-3 to ensure all NT stores retire before Phase 4/5 volatile writes or any
    // subsequent caller doorbell write. GCC uses regular STP stores (strongly ordered).

    // Further align destination to 16 bytes: STP Q / STNP Q to Device/UC memory (PCIe BAR)
    // require 16-byte-aligned addresses; Phase 0 only guaranteed 4-byte alignment.
    while (size >= 4 && (reinterpret_cast<std::uintptr_t>(d) % 16) != 0) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        d += 4;
        s += 4;
        size -= 4;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* d_wide = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(d));

    // aligned(1) does not suppress STNP: Clang emits ldp q1,q0 / stnp q1,q0 regardless
    // of the alignment attribute (verified via objdump on AArch64 with Clang 22).
    typedef std::uint8_t __attribute__((vector_size(32), aligned(1))) v32;
    typedef std::uint8_t __attribute__((vector_size(16), aligned(1))) v16;
    // 2 KB ahead: hides ~100 ns AArch64 DRAM latency at typical PCIe DMA write rates.
    constexpr std::size_t prefetch_distance = 2048;

    // Phase 1: 256-byte blocks (8 x 32-byte stores).
    // The prefetch is issued once per 32-byte chunk (not hoisted to the outer loop) so
    // that after unrolling the compiler issues 8 prfm instructions covering 8 distinct
    // cache lines (+2048..+2272 bytes ahead). Hoisting to one prefetch per 256-byte block
    // would fetch only a single cache line ahead, leaving the other 7 uncovered.
    while (size >= 256) {
        for (int j = 0; j < 8; ++j) {
            __builtin_prefetch(s + prefetch_distance, 0, 0);
            v32 chunk;
            __builtin_memcpy(&chunk, s, 32);
#if defined(__clang__)
            __builtin_nontemporal_store(chunk, reinterpret_cast<v32*>(d_wide));
#else
            __builtin_memcpy(d_wide, &chunk, 32);
#endif
            s += 32;
            d_wide += 32;
        }
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        __builtin_prefetch(s + prefetch_distance, 0, 0);
        v32 chunk;
        __builtin_memcpy(&chunk, s, 32);
#if defined(__clang__)
        __builtin_nontemporal_store(chunk, reinterpret_cast<v32*>(d_wide));
#else
        __builtin_memcpy(d_wide, &chunk, 32);
#endif
        s += 32;
        d_wide += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        v16 chunk;
        __builtin_memcpy(&chunk, s, 16);
#if defined(__clang__)
        __builtin_nontemporal_store(chunk, reinterpret_cast<v16*>(d_wide));
#else
        __builtin_memcpy(d_wide, &chunk, 16);
#endif
        s += 16;
        d_wide += 16;
        size -= 16;
    }

    // DMB ISH: ensure all STNP stores above are globally visible before Phase 4/5
    // volatile writes and before the function returns to the caller.
#if defined(__clang__)
    __atomic_thread_fence(__ATOMIC_RELEASE);
#endif
    d = reinterpret_cast<volatile std::uint8_t*>(d_wide);
#endif

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        auto t = std::chrono::steady_clock::now();
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        recorder.record(t, 4);
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
        recorder.record(t, 1);
        timer.record_and_check(t, 1);
    }
}

void memcpy_from_device(
    void* dest, const volatile void* src, std::size_t size, const std::function<bool()>& on_timeout) {
    const std::size_t original_size = size;
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    auto timer = make_mmio_timeout_guard("load", original_size, size, on_timeout);
    MemcpyTimingRecorder recorder("memcpy_from_device", original_size);

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        recorder.record(t, 1);
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
        recorder.record(t, 256);
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
        recorder.record(t, 32);
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
        recorder.record(t, 16);
        timer.record_and_check(t, 16);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        d += 16;
        s_simd += 16;
        size -= 16;
    }

    s = reinterpret_cast<const volatile std::uint8_t*>(s_simd);

#elif defined(__GNUC__)
    // GCC/Clang non-x86: vector extensions + __builtin_memcpy (LDP/STP on AArch64).

    // Further align source to 16 bytes: LDR Q / LDP Q from Device/UC memory (PCIe BAR)
    // require 16-byte-aligned addresses; Phase 0 only guaranteed 4-byte alignment.
    while (size >= 4 && (reinterpret_cast<std::uintptr_t>(s) % 16) != 0) {
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_wide = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    typedef std::uint8_t __attribute__((vector_size(32), aligned(1))) v32;
    typedef std::uint8_t __attribute__((vector_size(16), aligned(1))) v16;
    // 2 KB ahead: hides ~100 ns AArch64 DRAM latency at typical PCIe DMA read rates.
    constexpr std::size_t prefetch_distance = 2048;

    // Phase 1: 256-byte blocks (8 x 32-byte loads).
    while (size >= 256) {
        for (int j = 0; j < 8; ++j) {
            __builtin_prefetch(s_wide + prefetch_distance, 0, 0);
            v32 chunk;
            __builtin_memcpy(&chunk, s_wide, 32);
            __builtin_memcpy(d, &chunk, 32);
            s_wide += 32;
            d += 32;
        }
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        __builtin_prefetch(s_wide + prefetch_distance, 0, 0);
        v32 chunk;
        __builtin_memcpy(&chunk, s_wide, 32);
        __builtin_memcpy(d, &chunk, 32);
        s_wide += 32;
        d += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        v16 chunk;
        __builtin_memcpy(&chunk, s_wide, 16);
        __builtin_memcpy(d, &chunk, 16);
        s_wide += 16;
        d += 16;
        size -= 16;
    }

    s = reinterpret_cast<const volatile std::uint8_t*>(s_wide);
#endif

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        auto t = std::chrono::steady_clock::now();
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        recorder.record(t, 4);
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
        recorder.record(t, 1);
        timer.record_and_check(t, 1);
    }
}

}  // namespace tt::umd
