// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "device_memcpy.hpp"

#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace tt::umd {

void memcpy_to_device(volatile void* dest, const void* src, std::size_t size) {
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

#if defined(__x86_64__) || defined(_M_X64)
    // The AVX2 intrinsics below are not themselves volatile, so the compiler is free to
    // reorder them relative to other non-volatile accesses. The volatile byte/4-byte loops
    // in Phases 0/4/5 bound the reordering window for this function.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* d_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(d));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned stores).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 224));

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 32), v1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 64), v2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 96), v3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 128), v4);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 160), v5);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 192), v6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd + 224), v7);

        d_simd += 256;
        s += 256;
        size -= 256;
    }

    // Phase 2: Remaining 32-byte chunks.
    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d_simd), v);
        d_simd += 32;
        s += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d_simd), v);
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
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

void memcpy_from_device(void* dest, const volatile void* src, std::size_t size) {
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

#if defined(__x86_64__) || defined(_M_X64)
    // The AVX2 intrinsics below are not themselves volatile, so the compiler is free to
    // reorder them relative to other non-volatile accesses. The volatile byte/4-byte loops
    // in Phases 0/4/5 bound the reordering window for this function.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_simd = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    // Phase 1: Bulk 256-byte blocks (8 x 32-byte AVX2 unaligned loads).
    // After this loop, 0 <= size < 256; remaining bytes are handled by Phases 2-5.
    while (size >= 256) {
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s_simd + 224));

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
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v);
        d += 32;
        s_simd += 32;
        size -= 32;
    }

    // Phase 3: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s_simd));
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
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile loads.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

}  // namespace tt::umd
