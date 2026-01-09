// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <unordered_map>
#include <vector>

#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class Writer;
class TTDevice;

class TLBManager {
public:
    TLBManager(TTDevice* tt_device);

    // All tt_xy_pairs should be in TRANSLATED coords.
    void configure_tlb(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering);
    void configure_tlb_kmd(tt_xy_pair core, size_t tlb_size, uint64_t address, uint64_t ordering);
    bool is_tlb_mapped(tt_xy_pair core);
    bool is_tlb_mapped(tt_xy_pair core, uint64_t address, uint32_t size_in_bytes);

    Writer get_static_tlb_writer(tt_xy_pair core);
    tlb_configuration get_tlb_configuration(tt_xy_pair core);

    // TODO: the following members will be moved to private once enough stuff is moved out of cluster.
    std::unordered_map<int32_t, uint64_t> tlb_config_map_;
    std::unordered_map<tt_xy_pair, std::int32_t> map_core_to_tlb_;
    std::unordered_map<int32_t, std::unique_ptr<TlbWindow>> tlb_windows_;

    TTDevice* get_tt_device() { return tt_device_; }

    TlbWindow* get_tlb_window(const tt_xy_pair core);

    std::unique_ptr<TlbWindow> allocate_tlb_window(
        tlb_data config, const TlbMapping mapping = TlbMapping::WC, const size_t tlb_size = 0);

    // Clear all static TLB mappings.
    void clear_mapped_tlbs();

private:
    TTDevice* tt_device_;
};

}  // namespace tt::umd
