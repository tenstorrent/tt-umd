/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/mock_chip.h"

namespace tt::umd {

MockChip::MockChip(tt_SocDescriptor soc_descriptor) : Chip(soc_descriptor) {}

bool MockChip::is_mmio_capable() const { return false; }

void MockChip::start_device() {}

int MockChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    return 0;
}

void MockChip::l1_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<tt::umd::CoreCoord>& cores) {}

void MockChip::dram_membar(const std::unordered_set<uint32_t>& channels) {}
}  // namespace tt::umd
