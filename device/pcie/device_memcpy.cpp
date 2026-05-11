// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/pcie/device_memcpy.hpp"

#include <cstdint>
#include <cstring>

namespace tt::umd {

void streaming_memcpy_to_device(volatile void* dest, const void* src, std::size_t size) {
    auto* d = static_cast<volatile std::uint8_t*>(dest);
    auto* s = static_cast<const std::uint8_t*>(src);

    // Phase 0: Align destination to 4 bytes using byte-wide volatile stores.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(d) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

    // Phase 1: If destination is 4-byte but not 8-byte aligned, emit one 4-byte store
    // to reach 8-byte alignment before the bulk 8-byte loop.
    if (size >= 4 && (reinterpret_cast<std::uintptr_t>(d) % 8) != 0) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 2: 8-byte volatile stores for the aligned middle.
    while (size >= 8) {
        std::uint64_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        *reinterpret_cast<volatile std::uint64_t*>(d) = tmp;
        d += 8;
        s += 8;
        size -= 8;
    }

    // Phase 3: Trailing 4-byte chunk, if any.
    if (size >= 4) {
        std::uint32_t tmp;
        std::memcpy(&tmp, s, sizeof(tmp));
        *reinterpret_cast<volatile std::uint32_t*>(d) = tmp;
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 4: Trailing bytes (0-3) using byte-wide volatile stores.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

void streaming_memcpy_from_device(void* dest, const volatile void* src, std::size_t size) {
    auto* d = static_cast<std::uint8_t*>(dest);
    auto* s = static_cast<const volatile std::uint8_t*>(src);

    // Phase 0: Align source to 4 bytes using byte-wide volatile loads.
    while (size > 0 && (reinterpret_cast<std::uintptr_t>(s) % 4) != 0) {
        *d++ = *s++;
        size--;
    }

    // Phase 1: If source is 4-byte but not 8-byte aligned, emit one 4-byte load
    // to reach 8-byte alignment before the bulk 8-byte loop.
    if (size >= 4 && (reinterpret_cast<std::uintptr_t>(s) % 8) != 0) {
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 2: 8-byte volatile loads for the aligned middle.
    while (size >= 8) {
        std::uint64_t tmp = *reinterpret_cast<const volatile std::uint64_t*>(s);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 8;
        s += 8;
        size -= 8;
    }

    // Phase 3: Trailing 4-byte chunk, if any.
    if (size >= 4) {
        std::uint32_t tmp = *reinterpret_cast<const volatile std::uint32_t*>(s);
        std::memcpy(d, &tmp, sizeof(tmp));
        d += 4;
        s += 4;
        size -= 4;
    }

    // Phase 4: Trailing bytes (0-3) using byte-wide volatile loads.
    while (size > 0) {
        *d++ = *s++;
        size--;
    }
}

}  // namespace tt::umd
