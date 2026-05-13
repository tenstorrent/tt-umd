// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device_memcpy.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

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

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size) {
    memcpy_to_device(dest, src, size, std::chrono::steady_clock::time_point::max());
}

void memcpy_to_device(
    volatile void* dest, const void* src, std::size_t size, std::chrono::steady_clock::time_point deadline) {
    const std::size_t original_size = size;
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    auto check_deadline = [&]() {
        if (std::chrono::steady_clock::now() >= deadline) {
            auto past_deadline_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - deadline)
                    .count();
            throw error::DeviceTimeoutError(
                "memcpy_to_device timeout: " + std::to_string(size) + " of " + std::to_string(original_size) +
                " bytes remaining, " + std::to_string(past_deadline_ms) + " ms past deadline.");
        }
    };

    check_deadline();

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        *d++ = *s++;
        size--;
        check_deadline();
    }

#if defined(__x86_64__) || defined(_M_X64)
    // The AVX2 intrinsics below are not themselves volatile. Each check_deadline() call is
    // an opaque function call and therefore acts as a compiler barrier — it prevents the
    // compiler from reordering the volatile-conceptual SIMD load/store around it. The
    // explicit volatile loops in Phases 0/4/5 still bound the ordering for the byte/word
    // tails.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* d_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(d));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned stores).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        // Loads are from host memory (s) — no TLB access, no deadline check between them.
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 224));

        // Stores go to TLB-mapped device memory — check after each.
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v0);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 32), v1);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 64), v2);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 96), v3);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 128), v4);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 160), v5);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 192), v6);
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 224), v7);
        check_deadline();

        d_simd += 256;
        s += 256;
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v);
        check_deadline();
        d_simd += 32;
        s += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d_simd), v);
        check_deadline();
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
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        check_deadline();
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        *d++ = *s++;
        size--;
        check_deadline();
    }
}

void memcpy_from_device(void* dest, const volatile void* src, std::size_t size) {
    memcpy_from_device(dest, src, size, std::chrono::steady_clock::time_point::max());
}

void memcpy_from_device(
    void* dest, const volatile void* src, std::size_t size, std::chrono::steady_clock::time_point deadline) {
    const std::size_t original_size = size;
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    auto check_deadline = [&]() {
        if (std::chrono::steady_clock::now() >= deadline) {
            auto past_deadline_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - deadline)
                    .count();
            throw error::DeviceTimeoutError(
                "memcpy_from_device timeout: " + std::to_string(size) + " of " + std::to_string(original_size) +
                " bytes remaining, " + std::to_string(past_deadline_ms) + " ms past deadline.");
        }
    };

    check_deadline();

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        *d++ = *s++;
        size--;
        check_deadline();
    }

#if defined(__x86_64__) || defined(_M_X64)
    // The AVX2 intrinsics below are not themselves volatile. Each check_deadline() call is
    // an opaque function call and therefore acts as a compiler barrier — it prevents the
    // compiler from reordering the volatile-conceptual SIMD load/store around it. The
    // explicit volatile loops in Phases 0/4/5 still bound the ordering for the byte/word
    // tails.
    //
    // NOTE: inserting a check between loads serializes the non-posted PCIe reads, since
    // each check is a compiler/optimization barrier. This is intentional — the goal is
    // maximum responsiveness to a timeout — but it does reduce read-side throughput.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned loads).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        // Loads are from TLB-mapped device memory — check after each.
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        check_deadline();
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 32));
        check_deadline();
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 64));
        check_deadline();
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 96));
        check_deadline();
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 128));
        check_deadline();
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 160));
        check_deadline();
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 192));
        check_deadline();
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 224));
        check_deadline();

        // Stores go to host memory (d) — no TLB access, no deadline check between them.
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
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        check_deadline();
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v);
        d += 32;
        s_simd += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s_simd));
        check_deadline();
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        d += 16;
        s_simd += 16;
        size -= 16;
    }

    s = reinterpret_cast<const volatile std::uint8_t*>(s_simd);
#endif

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        check_deadline();
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile loads.
    while (size > 0) {
        *d++ = *s++;
        size--;
        check_deadline();
    }
}

}  // namespace tt::umd
