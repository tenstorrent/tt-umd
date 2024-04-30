/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "device/architecture.h"
#include "device/tlb.h"
#include "device/xy_pair.h"

namespace tt::umd {

class architecture_implementation {
   public:
    virtual ~architecture_implementation() = default;

    virtual architecture get_architecture() const = 0;
    virtual uint32_t get_arc_message_arc_get_harvesting() const = 0;
    virtual uint32_t get_arc_message_arc_go_busy() const = 0;
    virtual uint32_t get_arc_message_arc_go_long_idle() const = 0;
    virtual uint32_t get_arc_message_arc_go_short_idle() const = 0;
    virtual uint32_t get_arc_message_deassert_riscv_reset() const = 0;
    virtual uint32_t get_arc_message_get_aiclk() const = 0;
    virtual uint32_t get_arc_message_setup_iatu_for_peer_to_peer() const = 0;
    virtual uint32_t get_arc_message_test() const = 0;
    virtual uint32_t get_arc_csm_mailbox_offset() const = 0;
    virtual uint32_t get_arc_reset_arc_misc_cntl_offset() const = 0;
    virtual uint32_t get_arc_reset_scratch_offset() const = 0;
    virtual uint32_t get_dram_channel_0_peer2peer_region_start() const = 0;
    virtual uint32_t get_dram_channel_0_x() const = 0;
    virtual uint32_t get_dram_channel_0_y() const = 0;
    virtual uint32_t get_broadcast_tlb_index() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_base() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_size() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_cfg_addr() const = 0;
    virtual uint32_t get_mem_large_read_tlb() const = 0;
    virtual uint32_t get_mem_large_write_tlb() const = 0;
    virtual uint32_t get_static_tlb_cfg_addr() const = 0;
    virtual uint32_t get_static_tlb_size() const = 0;
    virtual uint32_t get_reg_tlb() const = 0;
    virtual uint32_t get_tlb_base_index_16m() const = 0;
    virtual uint32_t get_tensix_soft_reset_addr() const = 0;
    virtual uint32_t get_grid_size_x() const = 0;
    virtual uint32_t get_grid_size_y() const = 0;
    virtual uint32_t get_tlb_cfg_reg_size_bytes() const = 0;
    // Replace with std::span once we enable C++20
    virtual const std::vector<uint32_t>& get_harvesting_noc_locations() const = 0;
    virtual const std::vector<uint32_t>& get_t6_x_locations() const = 0;
    virtual const std::vector<uint32_t>& get_t6_y_locations() const = 0;

    virtual std::tuple<xy_pair, xy_pair> multicast_workaround(xy_pair start, xy_pair end) const = 0;
    virtual tlb_configuration get_tlb_configuration(uint32_t tlb_index) const = 0;
    virtual std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(std::int32_t tlb_index) const = 0;
    virtual std::pair<std::uint64_t, std::uint64_t> get_tlb_data(std::uint32_t tlb_index, const tlb_data& data) const = 0;

    static std::unique_ptr<architecture_implementation> create(architecture architecture);
};

}  // namespace tt::umd
