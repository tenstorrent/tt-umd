// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device_memcpy.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "umd/device/utils/error.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace tt::umd {

void write16_to_device(volatile void* dest, std::uint16_t value) {
    *reinterpret_cast<volatile std::uint16_t*>(dest) = value;
}

void write32_to_device(volatile void* dest, std::uint32_t value) {
    *reinterpret_cast<volatile std::uint32_t*>(dest) = value;
}

std::uint16_t read16_from_device(const volatile void* src) {
    return *reinterpret_cast<const volatile std::uint16_t*>(src);
}

std::uint32_t read32_from_device(const volatile void* src) {
    return *reinterpret_cast<const volatile std::uint32_t*>(src);
}

namespace {

// Hard-coded default per-op budget; overridable at process start via the env var
// TT_UMD_MMIO_OP_TIMEOUT_MS. Applied to every TLB-touching store/load. Set to 30 ms
// so post-reset reads (which can legitimately take >5 ms before the device settles)
// don't trip the timeout on the happy path.
constexpr std::chrono::milliseconds kDefaultMmioOpTimeout{30};

std::chrono::milliseconds mmio_op_timeout() {
    static const std::chrono::milliseconds value = [] {
        const char* env = std::getenv("TT_UMD_MMIO_OP_TIMEOUT_MS");
        if (env == nullptr || *env == '\0') {
            return kDefaultMmioOpTimeout;
        }
        try {
            return std::chrono::milliseconds(std::stoul(env));
        } catch (...) {
            return kDefaultMmioOpTimeout;
        }
    }();
    return value;
}

// Set TT_UMD_MEMCPY_TIMING=1 to instrument each TLB-touching store/load with
// a steady_clock delta and dump the deltas to stderr at function exit. Off by
// default — production builds pay only a single thread-local check per op
// when enabled, and nothing when disabled (the static-init lambda returns false).
bool mmio_timing_enabled() {
    static const bool value = [] {
        const char* env = std::getenv("TT_UMD_MEMCPY_TIMING");
        return env != nullptr && *env != '\0' && env[0] != '0';
    }();
    return value;
}

struct OpTiming {
    std::int64_t delta_ns;
    std::uint32_t size_bytes;
};

// Re-entrancy guard: if a MemcpyTimeoutFn callback itself ends up issuing MMIO
// that triggers a per-op timeout, the inner timeout must short-circuit (don't
// invoke the callback recursively — that would loop). The inner record_and_check
// sees the flag and treats the inner timeout as a confirmed abort.
thread_local bool in_timeout_callback = false;

struct ScopedTimeoutCallbackGuard {
    ScopedTimeoutCallbackGuard() { in_timeout_callback = true; }

    ~ScopedTimeoutCallbackGuard() { in_timeout_callback = false; }
};

void dump_timings(const char* fn, std::size_t bytes, const std::vector<OpTiming>& ops) {
    if (ops.empty()) {
        std::fprintf(stderr, "[%s] size=%zu ops=0\n", fn, bytes);
        return;
    }
    std::int64_t total = 0;
    std::int64_t min_ns = ops[0].delta_ns;
    std::int64_t max_ns = ops[0].delta_ns;
    for (const auto& op : ops) {
        total += op.delta_ns;
        min_ns = std::min(min_ns, op.delta_ns);
        max_ns = std::max(max_ns, op.delta_ns);
    }
    std::int64_t mean_ns = total / static_cast<std::int64_t>(ops.size());

    std::string line;
    line.reserve(ops.size() * 10 + 128);
    line.append("[").append(fn).append("] size=").append(std::to_string(bytes));
    line.append(" ops=").append(std::to_string(ops.size()));
    line.append(" min=").append(std::to_string(min_ns)).append("ns");
    line.append(" max=").append(std::to_string(max_ns)).append("ns");
    line.append(" mean=").append(std::to_string(mean_ns)).append("ns");
    line.append(" total=").append(std::to_string(total)).append("ns");
    line.append(" ops_ns:bytes=");
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (i != 0) {
            line.append(",");
        }
        line.append(std::to_string(ops[i].delta_ns));
        line.append(":");
        line.append(std::to_string(ops[i].size_bytes));
    }
    line.append("\n");
    std::fwrite(line.data(), 1, line.size(), stderr);
}

}  // namespace

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size, const MemcpyTimeoutFn& on_timeout) {
    const std::size_t original_size = size;
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    const auto op_timeout = mmio_op_timeout();
    const bool timing_enabled = mmio_timing_enabled();
    thread_local std::vector<OpTiming> timings;
    if (timing_enabled) {
        timings.clear();
    }

    // Called after every TLB-touching store. Single now() shared between
    // timing record and the per-op budget check.
    auto record_and_check = [&](std::chrono::steady_clock::time_point t_before, std::uint32_t op_bytes) {
        auto t_now = std::chrono::steady_clock::now();
        auto delta = t_now - t_before;
        if (timing_enabled) {
            auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
            timings.push_back({delta_ns, op_bytes});
        }
        if (delta < op_timeout) {
            return;
        }
        // Per-op budget exceeded. Consult on_timeout if available and we're not
        // already inside the callback (which would recurse).
        if (on_timeout && !in_timeout_callback) {
            ScopedTimeoutCallbackGuard guard;
            if (!on_timeout()) {
                return;  // false positive — device healthy, continue
            }
        }
        // Either no on_timeout provided, we're already inside one (skip recursion),
        // or on_timeout confirmed the abort. Throw.
        auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
        auto budget_ms = op_timeout.count();
        throw error::DeviceTimeoutError(
            "memcpy_to_device per-op timeout: " + std::to_string(op_bytes) + "B store took " +
            std::to_string(delta_ms) + " ms (budget=" + std::to_string(budget_ms) + " ms), " + std::to_string(size) +
            " of " + std::to_string(original_size) + " bytes remaining.");
    };

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        record_and_check(t, 1);
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
        record_and_check(t, 256);

        d_simd += 256;
        s += 256;
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        auto t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v);
        record_and_check(t, 32);
        d_simd += 32;
        s += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        auto t = std::chrono::steady_clock::now();
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d_simd), v);
        record_and_check(t, 16);
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
        record_and_check(t, 4);
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        record_and_check(t, 1);
    }

    if (timing_enabled) {
        dump_timings("memcpy_to_device", original_size, timings);
    }
}

void memcpy_from_device(void* dest, const volatile void* src, std::size_t size, const MemcpyTimeoutFn& on_timeout) {
    const std::size_t original_size = size;
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    const auto op_timeout = mmio_op_timeout();
    const bool timing_enabled = mmio_timing_enabled();
    thread_local std::vector<OpTiming> timings;
    if (timing_enabled) {
        timings.clear();
    }

    auto record_and_check = [&](std::chrono::steady_clock::time_point t_before, std::uint32_t op_bytes) {
        auto t_now = std::chrono::steady_clock::now();
        auto delta = t_now - t_before;
        if (timing_enabled) {
            auto delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
            timings.push_back({delta_ns, op_bytes});
        }
        if (delta < op_timeout) {
            return;
        }
        if (on_timeout && !in_timeout_callback) {
            ScopedTimeoutCallbackGuard guard;
            if (!on_timeout()) {
                return;
            }
        }
        auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
        auto budget_ms = op_timeout.count();
        throw error::DeviceTimeoutError(
            "memcpy_from_device per-op timeout: " + std::to_string(op_bytes) + "B load took " +
            std::to_string(delta_ms) + " ms (budget=" + std::to_string(budget_ms) + " ms), " + std::to_string(size) +
            " of " + std::to_string(original_size) + " bytes remaining.");
    };

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        auto t = std::chrono::steady_clock::now();
        *d++ = *s++;
        size--;
        record_and_check(t, 1);
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
        record_and_check(t, 256);

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
        record_and_check(t, 32);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v);
        d += 32;
        s_simd += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        auto t = std::chrono::steady_clock::now();
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s_simd));
        record_and_check(t, 16);
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
        record_and_check(t, 4);
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
        record_and_check(t, 1);
    }

    if (timing_enabled) {
        dump_timings("memcpy_from_device", original_size, timings);
    }
}

}  // namespace tt::umd
