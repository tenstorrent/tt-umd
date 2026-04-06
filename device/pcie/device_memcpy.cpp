// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/device_memcpy.hpp"

#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace tt::umd {

void streaming_memcpy_to_device(volatile void* dest, const void* src, std::size_t size) {
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    // Phase 0: Align device destination address to 4 bytes using byte-wide volatile stores.
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
    // This prepares for Phase 2 which uses _mm256_stream_si256 (VMOVNTDQ) to write
    // 256 bits (32 bytes) at a time — that instruction requires 32-byte alignment.
    while (size >= 4 && (reinterpret_cast<std::uintptr_t>(d_aligned) % 32) != 0) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        _mm_stream_si32(reinterpret_cast<int*>(d_aligned), static_cast<int>(tmp));
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
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        _mm_stream_si32(reinterpret_cast<int*>(d_aligned), static_cast<int>(tmp));
        d_aligned += 4;
        s += 4;
        size -= 4;
    }

    // Ensure all streaming stores are globally visible.
    _mm_sfence();
    // TODO: PCIe health check — after the sfence we could read a known device register
    // to detect a hung PCIe controller (0xFFFFFFFF readback = link down / completion timeout).

    d = reinterpret_cast<volatile std::uint8_t*>(d_aligned);

#else
    // Portable fallback: explicit 4-byte stores for the aligned middle.
    while (size >= 4) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        *reinterpret_cast<volatile std::uint32_t*>(const_cast<volatile std::uint8_t*>(d)) = tmp;
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

void streaming_memcpy_from_device(void* dest, const volatile void* src, std::size_t size) {
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

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

    // Phase 1: Align source to 32 bytes using 4-byte volatile loads.
    while (size >= 4 && (reinterpret_cast<std::uintptr_t>(s_aligned) % 32) != 0) {
        std::uint32_t tmp = *reinterpret_cast<volatile std::uint32_t*>(s_aligned);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s_aligned += 4;
        size -= 4;
    }

    // Phase 2: Bulk 256-byte blocks (8 x 32-byte AVX2 streaming loads).
    while (size >= 256) {
        __m256i v0 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned));
        __m256i v1 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 32));
        __m256i v2 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 64));
        __m256i v3 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 96));
        __m256i v4 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 128));
        __m256i v5 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 160));
        __m256i v6 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 192));
        __m256i v7 = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned + 224));

        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v0);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 32), v1);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 64), v2);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 96), v3);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 128), v4);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 160), v5);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 192), v6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d + 224), v7);

        d += 256;
        s_aligned += 256;
        size -= 256;
    }

    // Phase 3: Remaining 32-byte chunks.
    while (size >= 32) {
        __m256i v = _mm256_stream_load_si256(reinterpret_cast<const __m256i*>(s_aligned));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), v);
        d += 32;
        s_aligned += 32;
        size -= 32;
    }

    // Phase 3b: Remaining 16-byte chunk.
    if (size >= 16) {
        __m128i v = _mm_stream_load_si128(reinterpret_cast<__m128i*>(s_aligned));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), v);
        d += 16;
        s_aligned += 16;
        size -= 16;
    }

    // Phase 4: Remaining 4-byte chunks.
    while (size >= 4) {
        std::uint32_t tmp = *reinterpret_cast<volatile std::uint32_t*>(s_aligned);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s_aligned += 4;
        size -= 4;
    }

    s = reinterpret_cast<const volatile std::uint8_t*>(s_aligned);

#else
    // Portable fallback: explicit 4-byte loads for the aligned middle.
    while (size >= 4) {
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        std::memcpy(d, &tmp, sizeof(tmp));
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
