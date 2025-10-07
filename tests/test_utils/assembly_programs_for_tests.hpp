// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>

/*
godbolt link example:
    - https://godbolt.org/z/qne95Tso7

source code:
    int main() {
        int* a = (int*)0x10000;
        *a = 0x87654000;
        while (true);
    }
*/
inline constexpr std::array<uint32_t, 4> simple_brisc_program{
    0x000107b7,  // lui     a5, 0x10         ; a5 = 0x10000
    0x87654737,  // lui     a4, 0x87654      ; a4 = 0x87654000
    0x00e7a023,  // sw      a4, 0(a5)        ; store a4 at memory[a5 + 0]
    0x0000006f   // jal     zero, 0          ; infinite loop
};

/*
godbolt link example:
    - https://godbolt.org/z/zr3a7j48h
source code:
    int main() {
        volatile unsigned int* a = (unsigned int*)0x10000;
        *a = 0;
        while (true) {
            (*a)++;
        }
    }
*/
inline constexpr std::array<uint32_t, 6> counter_brisc_program{
    0x00010737,  // lui     a4, 0x10         ; a4 = 0x10000
    0x00072023,  // sw      zero, 0(a4)      ; clear memory
    0x00072783,  // lw      a5, 0(a4)        ; load word
    0x00178793,  // addi    a5, a5, 1        ; increment
    0x00f72023,  // sw      a5, 0(a4)        ; store back
    0xff5ff06f,  // jal     zero, -12        ; jump back to .L2
};

/*
godbolt link example:
    - https://godbolt.org/z/h3snvs595

source code:
    int main() {
        static constexpr unsigned int TRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en {0xFFEF'0000 + 4*161};
        static constexpr unsigned int NCRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en {0xFFEF'0000 + 4*163};
        unsigned int* trisc_overrride_enable_reg_addr = (unsigned int*)TRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en;
        unsigned int* ncrisc_overrride_enable_reg_addr = (unsigned int*)NCRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en;
        *trisc_overrride_enable_reg_addr = 0xFFFF'FFFF & 0x0000'0007;
        *ncrisc_overrride_enable_reg_addr = 0xFFFF'FFFF & 0x0000'0001;

        static constexpr unsigned int TRISC_RESET_PC_SEC0_PC {0xFFEF'0000 + 4*158};
        static constexpr unsigned int TRISC_RESET_PC_SEC1_PC {0xFFEF'0000 + 4*159};
        static constexpr unsigned int TRISC_RESET_PC_SEC2_PC {0xFFEF'0000 + 4*160};
        static constexpr unsigned int NCRISC_RESET_PC_PC {0xFFEF'0000 + 4*162};
        unsigned int* trisc0_code_start_reg_addr = (unsigned int*)TRISC_RESET_PC_SEC0_PC;
        unsigned int* trisc1_code_start_reg_addr = (unsigned int*)TRISC_RESET_PC_SEC1_PC;
        unsigned int* trisc2_code_start_reg_addr = (unsigned int*)TRISC_RESET_PC_SEC2_PC;
        unsigned int* ncrisc_code_start_reg_addr = (unsigned int*)NCRISC_RESET_PC_PC;

        *trisc0_code_start_reg_addr = 0x2'0000;
        *trisc1_code_start_reg_addr = 0x3'0000;
        *trisc2_code_start_reg_addr = 0x4'0000;
        *ncrisc_code_start_reg_addr = 0x5'0000;

        while (true);
    }
*/
inline constexpr std::array<uint32_t, 14> wh_brisc_configuration_program{
    0xffef07b7,  // lui    a5,0xffef0       ; Load upper immediate
    0x00700713,  // li     a4,7             ; addi a4, zero, 7
    0x28e7a223,  // sw     a4,644(a5)       ; store a4 at offset 644 from a5
    0x00100713,  // li     a4,1             ; addi a4, zero, 1
    0x28e7a623,  // sw     a4,652(a5)       ; store a4 at offset 652 from a5
    0x00020737,  // lui    a4,0x20          ; load upper immediate 0x20 into a4
    0x26e7ac23,  // sw     a4,632(a5)       ; store a4 at offset 632 from a5
    0x00030737,  // lui    a4,0x30          ; load upper immediate 0x30 into a4
    0x26e7ae23,  // sw     a4,636(a5)       ; store a4 at offset 636 from a5
    0x00040737,  // lui    a4,0x40          ; load upper immediate 0x40 into a4
    0x28e7a023,  // sw     a4,640(a5)      ; store a4 at offset 640 from a5
    0x00050737,  // lui    a4,0x50         ; load upper immediate 0x50 into a4
    0x28e7a423,  // sw     a4,648(a5)      ; store a4 at offset 648 from a5
    0x0000006f   // jal    zero, 0         ; infinite loop
};

/*
godbolt link example:
    - https://godbolt.org/z/qM9nxs7ec

source code:
    int main() {
        static constexpr unsigned int TRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en {0xFFB1'2000 + 0x234};
        static constexpr unsigned int NCRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en {0xFFB1'2000 + 0x23C};
        unsigned int* trisc_overrride_enable_reg_addr = (unsigned int*)TRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en;
        unsigned int* ncrisc_overrride_enable_reg_addr = (unsigned int*)NCRISC_RESET_PC_OVERRIDE_Reset_PC_Override_en;
        *trisc_overrride_enable_reg_addr = 0xFFFF'FFFF & 0x0000'0007;
        *ncrisc_overrride_enable_reg_addr = 0xFFFF'FFFF & 0x0000'0001;

        static constexpr unsigned int TRISC_RESET_PC_SEC0_PC {0xFFB1'2000 + 0x228};
        static constexpr unsigned int TRISC_RESET_PC_SEC1_PC {0xFFB1'2000 + 0x22C};
        static constexpr unsigned int TRISC_RESET_PC_SEC2_PC {0xFFB1'2000 + 0x230};
        static constexpr unsigned int NCRISC_RESET_PC_PC {0xFFB1'2000 + 0x238};
        unsigned int* trisc0_code_start_reg_addr = (unsigned int*)TRISC_RESET_PC_SEC0_PC;
        unsigned int* trisc1_code_start_reg_addr = (unsigned int*)TRISC_RESET_PC_SEC1_PC;
        unsigned int* trisc2_code_start_reg_addr = (unsigned int*)TRISC_RESET_PC_SEC2_PC;
        unsigned int* ncrisc_code_start_reg_addr = (unsigned int*)NCRISC_RESET_PC_PC;

        *trisc0_code_start_reg_addr = 0x2'0000;
        *trisc1_code_start_reg_addr = 0x3'0000;
        *trisc2_code_start_reg_addr = 0x4'0000;
        *ncrisc_code_start_reg_addr = 0x5'0000;

        while (true);
    }
*/
inline constexpr std::array<uint32_t, 14> bh_brisc_configuration_program{
    0xffb127b7,  // lui    a5,0xffb12        ; a5 = 0xffb12000
    0x00700713,  // li     a4,7              ; addi a4, zero, 7
    0x22e7aa23,  // sw     a4,564(a5)        ; *(a5 + 564) = a4 (1)
    0x00100713,  // li     a4,1              ; a4 = 1
    0x22e7ae23,  // sw     a4,572(a5)        ; *(a5 + 572) = a4 (1)
    0x00020737,  // lui    a4,0x20           ; a4 = 0x20000
    0x22e7a423,  // sw     a4,552(a5)        ; *(a5 + 552) = a4 (0x20000)
    0x00030737,  // lui    a4,0x30           ; a4 = 0x30000
    0x22e7a623,  // sw     a4,556(a5)        ; *(a5 + 556) = a4 (0x30000)
    0x00040737,  // lui    a4,0x40           ; a4 = 0x40000
    0x22e7a823,  // sw     a4,560(a5)       ; *(a5 + 560) = a4 (0x40000)
    0x00050737,  // lui    a4,0x50          ; a4 = 0x50000
    0x22e7ac23,  // sw     a4,568(a5)       ; *(a5 + 568) = a4 (0x50000)
    0x0000006f   // jal    zero, 0          ; infinite loop
};
