// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

inline constexpr std::array<uint32_t, 4> brisc_program{
    0x000107b7,  // lui a5,0x10
    0x87654737,  // lui a4,0x87654
    0x00e7a023,  // sw  a4,0(a5)
    0x0000006f   // jal x0, 0
};
