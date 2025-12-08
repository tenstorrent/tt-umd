/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

static const uint32_t HANG_READ_VALUE = 0xFFFFFFFFu;

class architecture_implementation {
public:
    virtual ~architecture_implementation() = default;

    virtual tt::ARCH get_architecture() const = 0;
    virtual uint32_t get_arc_message_arc_get_harvesting() const = 0;
    virtual uint32_t get_arc_message_arc_go_busy() const = 0;
    virtual uint32_t get_arc_message_arc_go_long_idle() const = 0;
    virtual uint32_t get_arc_message_arc_go_short_idle() const = 0;
    virtual uint32_t get_arc_message_deassert_riscv_reset() const = 0;
    virtual uint32_t get_arc_message_get_aiclk() const = 0;
    virtual uint32_t get_arc_message_setup_iatu_for_peer_to_peer() const = 0;
    virtual uint32_t get_arc_message_test() const = 0;
    virtual uint32_t get_arc_csm_bar0_mailbox_offset() const = 0;
    virtual uint32_t get_arc_axi_apb_peripheral_offset() const = 0;
    virtual uint32_t get_arc_reset_arc_misc_cntl_offset() const = 0;
    virtual uint32_t get_arc_reset_scratch_offset() const = 0;
    virtual uint32_t get_arc_reset_scratch_2_offset() const = 0;
    virtual uint32_t get_arc_reset_unit_refclk_low_offset() const = 0;
    virtual uint32_t get_arc_reset_unit_refclk_high_offset() const = 0;
    virtual uint32_t get_dram_channel_0_peer2peer_region_start() const = 0;
    virtual uint32_t get_dram_channel_0_x() const = 0;
    virtual uint32_t get_dram_channel_0_y() const = 0;
    virtual uint32_t get_dram_banks_number() const = 0;
    virtual uint32_t get_broadcast_tlb_index() const = 0;
    virtual uint32_t get_dynamic_tlb_2m_base() const = 0;
    virtual uint32_t get_dynamic_tlb_2m_size() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_base() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_size() const = 0;
    virtual uint32_t get_dynamic_tlb_16m_cfg_addr() const = 0;
    virtual uint32_t get_mem_large_read_tlb() const = 0;
    virtual uint32_t get_mem_large_write_tlb() const = 0;
    virtual uint32_t get_num_eth_channels() const = 0;
    virtual uint32_t get_static_tlb_cfg_addr() const = 0;
    virtual uint32_t get_static_tlb_size() const = 0;
    virtual uint32_t get_read_checking_offset() const = 0;
    virtual uint32_t get_reg_tlb() const = 0;
    virtual uint32_t get_tlb_base_index_16m() const = 0;
    virtual uint32_t get_tensix_soft_reset_addr() const = 0;
    virtual uint32_t get_debug_reg_addr() const = 0;
    virtual uint32_t get_soft_reset_reg_value(RiscType risc_type) const = 0;
    virtual RiscType get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const = 0;
    virtual uint32_t get_soft_reset_staggered_start() const = 0;
    virtual uint32_t get_grid_size_x() const = 0;
    virtual uint32_t get_grid_size_y() const = 0;
    virtual uint64_t get_arc_apb_noc_base_address() const = 0;
    virtual uint64_t get_arc_csm_noc_base_address() const = 0;
    // Replace with std::span once we enable C++20.
    virtual const std::vector<uint32_t>& get_harvesting_noc_locations() const = 0;
    virtual const std::vector<uint32_t>& get_t6_x_locations() const = 0;
    virtual const std::vector<uint32_t>& get_t6_y_locations() const = 0;
    virtual const std::vector<std::vector<tt_xy_pair>>& get_dram_cores_noc0() const = 0;

    // TLB related. Move other functions here as well.
    virtual std::pair<uint32_t, uint32_t> get_tlb_1m_base_and_count() const = 0;
    virtual std::pair<uint32_t, uint32_t> get_tlb_2m_base_and_count() const = 0;
    virtual std::pair<uint32_t, uint32_t> get_tlb_16m_base_and_count() const = 0;
    virtual std::pair<uint32_t, uint32_t> get_tlb_4g_base_and_count() const = 0;

    virtual std::tuple<xy_pair, xy_pair> multicast_workaround(xy_pair start, xy_pair end) const = 0;
    virtual tlb_configuration get_tlb_configuration(uint32_t tlb_index) const = 0;

    virtual DeviceL1AddressParams get_l1_address_params() const = 0;
    virtual DriverHostAddressParams get_host_address_params() const = 0;
    virtual DriverEthInterfaceParams get_eth_interface_params() const = 0;
    virtual DriverNocParams get_noc_params() const = 0;

    static std::unique_ptr<architecture_implementation> create(tt::ARCH architecture);

    virtual uint64_t get_noc_node_id_offset() const = 0;
    virtual uint64_t get_noc_reg_base(
        const CoreType core_type, const uint32_t noc, const uint32_t noc_port = 0) const = 0;

    // Get preferred tlb size, which is the tlb group with the largest count available.
    virtual size_t get_cached_tlb_size() const = 0;

    // Whether static_vc should be used for tlb configuration.
    virtual bool get_static_vc() const = 0;
};

}  // namespace tt::umd
