/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_xy_pair.h"
#include "umd/device/types/tlb.h"

namespace tt {
class Writer;
}

namespace tt::umd {

class TTDevice;

class TLBManager {
public:
    TLBManager(TTDevice* tt_device);

    void configure_tlb(tt_xy_pair core, int32_t tlb_index, uint64_t address, uint64_t ordering);

    void set_dynamic_tlb(std::string fallback_tlb_name, int32_t tlb_index);
    void set_dynamic_tlb_ordering(std::string fallback_tlb_name, uint64_t ordering);

    bool address_in_tlb_space(uint64_t address, uint32_t size_in_bytes, int32_t tlb_index, uint64_t tlb_size);
    bool is_tlb_mapped(tt_xy_pair core);
    bool is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes);

    tt::Writer get_static_tlb_writer(tt_xy_pair core);
    tlb_configuration get_tlb_configuration(tt_xy_pair core);

    // TODO: the following members will be moved to private once enough stuff is moved out of cluster.
    std::unordered_map<int32_t, uint64_t> tlb_config_map_;
    std::unordered_map<tt_xy_pair, std::int32_t> map_core_to_tlb_;

    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config_;
    std::unordered_map<std::string, uint64_t> dynamic_tlb_ordering_modes_;

private:
    TTDevice* tt_device_;
};

}  // namespace tt::umd
