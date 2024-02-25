/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <string>

#include "device/tlb.h"

using TLB_OFFSETS = tt::umd::tlb_offsets;
using TLB_DATA = tt::umd::tlb_data;

typedef struct {
    uint32_t  chip_addr;
    uint32_t  host_phys_addr;
    uint32_t  completion_flag_phys_addr;
    uint32_t  size_bytes                  : 28;
    uint32_t  write                       : 1;
    uint32_t  pcie_msi_on_done            : 1;
    uint32_t  pcie_write_on_done          : 1;
    uint32_t  trigger                     : 1;
    uint32_t  repeat;
} arc_pcie_ctrl_dma_request_t; // 5 * 4 = 20B

enum class TensixSoftResetOptions: std::uint32_t {
    NONE = 0,
    BRISC = ((std::uint32_t) 1 << 11),
    TRISC0 = ((std::uint32_t) 1 << 12),
    TRISC1 = ((std::uint32_t) 1 << 13),
    TRISC2 = ((std::uint32_t) 1 << 14),
    NCRISC = ((std::uint32_t) 1 << 18),
    STAGGERED_START = ((std::uint32_t) 1 << 31)
};

std::string TensixSoftResetOptionsToString(TensixSoftResetOptions value);
constexpr TensixSoftResetOptions operator|(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return static_cast<TensixSoftResetOptions>(
        static_cast<uint32_t>(lhs) |
        static_cast<uint32_t>(rhs)
    );
}

constexpr TensixSoftResetOptions operator&(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return static_cast<TensixSoftResetOptions>(
        static_cast<uint32_t>(lhs) &
        static_cast<uint32_t>(rhs)
    );
}

constexpr bool operator!=(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return
        static_cast<uint32_t>(lhs) !=
        static_cast<uint32_t>(rhs);
}

static constexpr TensixSoftResetOptions ALL_TRISC_SOFT_RESET = TensixSoftResetOptions::TRISC0 |
                                                           TensixSoftResetOptions::TRISC1 |
                                                           TensixSoftResetOptions::TRISC2;

static constexpr TensixSoftResetOptions ALL_TENSIX_SOFT_RESET = TensixSoftResetOptions::BRISC |
                                                            TensixSoftResetOptions::NCRISC |
                                                            TensixSoftResetOptions::STAGGERED_START |
                                                            ALL_TRISC_SOFT_RESET;

static constexpr TensixSoftResetOptions TENSIX_ASSERT_SOFT_RESET = TensixSoftResetOptions::BRISC |
                                                               TensixSoftResetOptions::NCRISC |
                                                               ALL_TRISC_SOFT_RESET;

static constexpr TensixSoftResetOptions TENSIX_DEASSERT_SOFT_RESET = TensixSoftResetOptions::NCRISC |
                                                                 ALL_TRISC_SOFT_RESET |
                                                                 TensixSoftResetOptions::STAGGERED_START;

static constexpr TensixSoftResetOptions TENSIX_DEASSERT_SOFT_RESET_NO_STAGGER = TensixSoftResetOptions::NCRISC |
                                                                                 ALL_TRISC_SOFT_RESET;
