// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::umd {

// Width of each device access in a register transfer. A register must be accessed at its own width, so a
// transfer is issued as one access of this width per register.
enum class RegisterAccessWidth : uint8_t { Bits32, Bits64 };

constexpr size_t register_access_size(RegisterAccessWidth width) {
    return width == RegisterAccessWidth::Bits64 ? sizeof(uint64_t) : sizeof(uint32_t);
}

}  // namespace tt::umd
