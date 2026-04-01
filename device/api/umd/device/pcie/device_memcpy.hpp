// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace tt::umd {

/**
 * Streaming memcpy for writes targeting device memory mapped through a TLB window.
 *
 * Standard memcpy (glibc) can emit overlapping stores to the same address, which causes
 * double writes when the destination is device memory. This routine guarantees each
 * destination address is written exactly once.
 *
 * On x86_64: uses non-temporal (streaming) stores via AVX2/SSE, bypassing CPU cache
 * for better throughput than memcpy on write-combining memory.
 *
 * On other architectures: falls back to explicit single-byte/4-byte stores (no double writes).
 *
 * Handles arbitrary alignment and size — uses read-modify-write for leading/trailing
 * partial 4-byte words on the device side.
 */
inline void streaming_memcpy_to_device(void* dest, const void* src, std::size_t size) {
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    // Phase 0: Align device destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

#if defined(__x86_64__) || defined(_M_X64)
    // d is now 4-byte aligned. Cast to non-volatile for SIMD intrinsics — the intrinsics
    // are opaque to the compiler and won't be reordered or eliminated.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* d_aligned = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(d));

    // Phase 1: Align destination to 32 bytes using 4-byte streaming stores.
    while (size >= 4 && (reinterpret_cast<std::uintptr_t>(d_aligned) % 32) != 0) {
        _mm_stream_si32(
            reinterpret_cast<int*>(d_aligned), static_cast<int>(*reinterpret_cast<const std::uint32_t*>(s)));
        d_aligned += 4;
        s += 4;
        size -= 4;
    }

    // Phase 2: Bulk 256-byte blocks (8 x 32-byte AVX2 streaming stores).
    while (size >= 256) {
        __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
        __m256i v2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 64));
        __m256i v3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 96));
        __m256i v4 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 128));
        __m256i v5 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 160));
        __m256i v6 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 192));
        __m256i v7 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 224));

        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned), v0);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 32), v1);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 64), v2);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 96), v3);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 128), v4);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 160), v5);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 192), v6);
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned + 224), v7);

        d_aligned += 256;
        s += 256;
        size -= 256;
    }

    // Phase 3: Remaining 32-byte chunks.
    while (size >= 32) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d_aligned), v);
        d_aligned += 32;
        s += 32;
        size -= 32;
    }

    // Phase 4: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        _mm_stream_si128(reinterpret_cast<__m128i*>(d_aligned), v);
        d_aligned += 16;
        s += 16;
        size -= 16;
    }

    // Phase 5: Remaining 4-byte chunks.
    while (size >= 4) {
        _mm_stream_si32(
            reinterpret_cast<int*>(d_aligned), static_cast<int>(*reinterpret_cast<const std::uint32_t*>(s)));
        d_aligned += 4;
        s += 4;
        size -= 4;
    }

    // Ensure all streaming stores are globally visible.
    _mm_sfence();

    d = reinterpret_cast<volatile std::uint8_t*>(d_aligned);

#else
    // Portable fallback: explicit 4-byte stores for the aligned middle.
    while (size >= 4) {
        *reinterpret_cast<volatile std::uint32_t*>(const_cast<volatile std::uint8_t*>(d)) =
            *reinterpret_cast<const std::uint32_t*>(s);
        d += 4;
        s += 4;
        size -= 4;
    }
#endif

    // Phase 6: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

/**
 * Streaming memcpy for reads from device memory mapped through a TLB window.
 *
 * On x86_64: uses non-temporal (streaming) loads via SSE4.1 (MOVNTDQA), avoiding
 * CPU cache pollution from device data that won't be reused.
 *
 * On other architectures: falls back to explicit single-byte/4-byte loads.
 *
 * Handles arbitrary alignment and size.
 */
inline void streaming_memcpy_from_device(void* dest, void* src, std::size_t size) {
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<volatile std::uint8_t*>(src);

    // Phase 0: Align device source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

#if defined(__x86_64__) || defined(_M_X64)
    // s is now 4-byte aligned. Cast to non-volatile for SIMD intrinsics — the intrinsics
    // are opaque to the compiler and won't be reordered or eliminated.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast).
    auto* s_aligned = const_cast<std::uint8_t*>(static_cast<const volatile std::uint8_t*>(s));

    // Phase 1: Align source to 16 bytes using 4-byte volatile loads.
    while (size >= 4 && (reinterpret_cast<std::uintptr_t>(s_aligned) % 16) != 0) {
        *reinterpret_cast<std::uint32_t*>(d) = *reinterpret_cast<volatile std::uint32_t*>(s_aligned);
        d += 4;
        s_aligned += 4;
        size -= 4;
    }

    // Phase 2: Bulk 256-byte blocks (16 x 16-byte SSE streaming loads).
    while (size >= 256) {
        __m128i v0 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned));
        __m128i v1 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 16));
        __m128i v2 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 32));
        __m128i v3 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 48));
        __m128i v4 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 64));
        __m128i v5 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 80));
        __m128i v6 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 96));
        __m128i v7 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 112));
        __m128i v8 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 128));
        __m128i v9 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 144));
        __m128i v10 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 160));
        __m128i v11 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 176));
        __m128i v12 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 192));
        __m128i v13 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 208));
        __m128i v14 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 224));
        __m128i v15 = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned + 240));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 16), v1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 32), v2);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 48), v3);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 64), v4);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 80), v5);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 96), v6);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 112), v7);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 128), v8);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 144), v9);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 160), v10);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 176), v11);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 192), v12);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 208), v13);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 224), v14);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 240), v15);

        d += 256;
        s_aligned += 256;
        size -= 256;
    }

    // Phase 3: Remaining 16-byte chunks.
    while (size >= 16) {
        __m128i v = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        d += 16;
        s_aligned += 16;
        size -= 16;
    }

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        *reinterpret_cast<std::uint32_t*>(d) = *reinterpret_cast<volatile std::uint32_t*>(s_aligned);
        d += 4;
        s_aligned += 4;
        size -= 4;
    }

    s = reinterpret_cast<volatile std::uint8_t*>(s_aligned);

#else
    // Portable fallback: explicit 4-byte loads for the aligned middle.
    while (size >= 4) {
        *reinterpret_cast<std::uint32_t*>(d) =
            *reinterpret_cast<volatile std::uint32_t*>(const_cast<volatile std::uint8_t*>(s));
        d += 4;
        s += 4;
        size -= 4;
    }
#endif

    // Phase 5: Trailing bytes (0-3) using byte-wide volatile loads.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

}  // namespace tt::umd
