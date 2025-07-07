// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

inline constexpr std::array<uint32_t, 4> simple_brisc_program{
    0x000107b7,  // lui a5,0x10
    0x87654737,  // lui a4,0x87654
    0x00e7a023,  // sw  a4,0(a5)
    0x0000006f   // jal x0, 0
};

inline std::array<uint32_t, 6> counter_brisc_program{
    0x00010737,  // lui     a4, 0x10       ; a4 = 0x10000
    0x00072023,  // sw      zero, 0(a4)    ; clear memory
    0x00072783,  // lw      a5, 0(a4)      ; load word
    0x00178793,  // addi    a5, a5, 1      ; increment
    0x00f72023,  // sw      a5, 0(a4)      ; store back
    0xff5ff06f,  // jal     zero, -12      ; jump back to .L2
};
