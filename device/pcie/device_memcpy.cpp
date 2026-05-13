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
// TT_UMD_MMIO_OP_TIMEOUT_MS. Applied to every TLB-touching store/load.
constexpr std::chrono::milliseconds kDefaultMmioOpTimeout{5};

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

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size) {
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
        if (delta >= op_timeout) {
            auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
            auto budget_ms = op_timeout.count();
            throw error::DeviceTimeoutError(
                "memcpy_to_device per-op timeout: " + std::to_string(op_bytes) + "B store took " +
                std::to_string(delta_ms) + " ms (budget=" + std::to_string(budget_ms) + " ms), " +
                std::to_string(size) + " of " + std::to_string(original_size) + " bytes remaining.");
        }
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
    // explicit volatile loops in Phases 0/4/5 still bound the ordering for the byte/word
    // tails.
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

        // Stores go to TLB-mapped device memory — timed and checked after each.
        auto t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v0);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 32), v1);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 64), v2);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 96), v3);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 128), v4);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 160), v5);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 192), v6);
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 224), v7);
        record_and_check(t, 32);

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

void memcpy_from_device(void* dest, const volatile void* src, std::size_t size) {
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
        if (delta >= op_timeout) {
            auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
            auto budget_ms = op_timeout.count();
            throw error::DeviceTimeoutError(
                "memcpy_from_device per-op timeout: " + std::to_string(op_bytes) + "B load took " +
                std::to_string(delta_ms) + " ms (budget=" + std::to_string(budget_ms) + " ms), " +
                std::to_string(size) + " of " + std::to_string(original_size) + " bytes remaining.");
        }
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
    // NOTE: inserting a check between loads serializes the non-posted PCIe reads, since
    // each check is a compiler/optimization barrier. This is intentional — the goal is
    // maximum responsiveness to a timeout — but it does reduce read-side throughput.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned loads).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        // Loads are from TLB-mapped device memory — timed and checked after each.
        auto t = std::chrono::steady_clock::now();
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 32));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 64));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 96));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 128));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 160));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 192));
        record_and_check(t, 32);
        t = std::chrono::steady_clock::now();
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 224));
        record_and_check(t, 32);

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
