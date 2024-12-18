/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_xy_pair.h"

namespace tt::umd {

class TTDevice;

class TLBManager {
public:
    TLBManager(TTDevice* tt_device);
    void configure_tlb(tt_xy_pair core, int32_t tlb_index, uint64_t address, uint64_t ordering);

private:
    TTDevice* tt_device_;
    std::unordered_map<int32_t, uint64_t> tlb_config_map_;
    std::unordered_map<tt_xy_pair, std::int32_t> map_core_to_tlb_;
};

}  // namespace tt::umd
