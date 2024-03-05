/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>

#include "device/architecture_implementation.h"
#include "device/tlb.h"

namespace tt::umd {

namespace blackhole {

// clang-format off
// See src/t6ifc/t6py/packages/tenstorrent/data/wormhole/pci/tlb.yaml
// local_offset: [ 0, 15,  0,  "36-bit address prefix, prepended to the 20 LSBs of issued address to form a 56-bit NOC address. The 1MB TLB #n corresponds to the 1MB MMIO range starting at (0x0 + N*0x100000)."]
// x_end       : [ 0, 21, 16,  "" ]
// y_end       : [ 0, 27, 22,  "" ]
// x_start     : [ 0, 33, 28,  "" ]
// y_start     : [ 0, 39, 34,  "" ]
// noc_sel:      [ 0, 40, 40,  "NOC select (1 = NOC1, 0 = NOC0)"]
// mcast:        [ 0, 41, 41,  "1 = multicast, 0 = unicast"]
// ordering:     [ 0, 43, 42,  "ordering mode (01 = strict (full AXI ordering), 00 = relaxed (no RAW hazard), 10 = posted (may have RAW hazard)"]
// linked:       [ 0, 44, 44,  "linked"]
// clang-format on
static constexpr auto TLB_1M_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 16,
    .y_end = 22,
    .x_start = 28,
    .y_start = 34,
    .noc_sel = 40,
    .mcast = 41,
    .ordering = 42,
    .linked = 44,
    .static_vc = 45,
    .static_vc_end = 46};

// clang-format off
// local_offset: [ 0, 14,  0,  "35-bit address prefix, prepended to the 21 LSBs of issued address to form a 56-bit NOC address. The 2MB TLB #n corresponds to the 2MB MMIO range starting at (0x9C00000 + N*0x200000)."]
// x_end       : [ 0, 20, 15,  "" ]
// y_end       : [ 0, 26, 21,  "" ]
// x_start     : [ 0, 32, 27,  "" ]
// y_start     : [ 0, 38, 33,  "" ]
// noc_sel:      [ 0, 39, 39,  "NOC select (1 = NOC1, 0 = NOC0)"]
// mcast:        [ 0, 40, 40,  "1 = multicast, 0 = unicast"]
// ordering:     [ 0, 42, 41,  "ordering mode (01 = strict (full AXI ordering), 00 = relaxed (no RAW hazard), 10 = posted (may have RAW hazard)"]
// linked:       [ 0, 43, 43,  "linked"]
// clang-format on
static constexpr auto TLB_2M_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 15,
    .y_end = 21,
    .x_start = 27,
    .y_start = 33,
    .noc_sel = 39,
    .mcast = 40,
    .ordering = 41,
    .linked = 43,
    .static_vc = 44,
    .static_vc_end = 45};

// clang-format off
// local_offset: [ 0, 11,  0,  "32-bit address prefix, prepended to the 24 LSBs of issued address to form a 56-bit NOC address. The 16MB TLB #n corresponds to the 16MB MMIO range starting at (0xB000000 + N*0x1000000)."]
// x_end       : [ 0, 17, 12,  "" ]
// y_end       : [ 0, 23, 18,  "" ]
// x_start     : [ 0, 29, 24,  "" ]
// y_start     : [ 0, 35, 30,  "" ]
// noc_sel:      [ 0, 36, 36,  "NOC select (1 = NOC1, 0 = NOC0)"]
// mcast:        [ 0, 37, 37,  "1 = multicast, 0 = unicast"]
// ordering:     [ 0, 39, 38,  "ordering mode (01 = strict (full AXI ordering), 00 = relaxed (no RAW hazard), 10 = posted (may have RAW hazard)"]
// linked:       [ 0, 40, 40,  "linked"]
// clang-format on
static constexpr auto TLB_16M_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 12,
    .y_end = 18,
    .x_start = 24,
    .y_start = 30,
    .noc_sel = 36,
    .mcast = 37,
    .ordering = 38,
    .linked = 40,
    .static_vc = 41,
    .static_vc_end = 42};

enum class arc_message_type {
    NOP = 0x11,  // Do nothing
    GET_AICLK = 0x34,
    ARC_GO_BUSY = 0x52,
    ARC_GO_SHORT_IDLE = 0x53,
    ARC_GO_LONG_IDLE = 0x54,
    ARC_GET_HARVESTING = 0x57,
    SET_ETH_DRAM_TRAINED_STATUS = 0x58,
    TEST = 0x90,
    SETUP_IATU_FOR_PEER_TO_PEER = 0x97,
    DEASSERT_RISCV_RESET = 0xba
};

// DEVICE_DATA
static constexpr std::array<xy_pair, 18> DRAM_LOCATIONS = {
    {{0, 0},
     {5, 0},
     {0, 1},
     {5, 1},
     {5, 2},
     {5, 3},
     {5, 4},
     {0, 5},
     {5, 5},
     {0, 6},
     {5, 6},
     {0, 7},
     {5, 7},
     {5, 8},
     {5, 9},
     {5, 10},
     {0, 11},
     {5, 11}}};
static constexpr std::array<xy_pair, 1> ARC_LOCATIONS = {{{0, 2}}};
static constexpr std::array<xy_pair, 1> PCI_LOCATIONS = {{{0, 4}}};
static constexpr std::array<xy_pair, 16> ETH_LOCATIONS = {
    {{1, 0},
     {2, 0},
     {3, 0},
     {4, 0},
     {6, 0},
     {7, 0},
     {8, 0},
     {9, 0},
     {1, 6},
     {2, 6},
     {3, 6},
     {4, 6},
     {6, 6},
     {7, 6},
     {8, 6},
     {9, 6}}};
// Return to std::array instead of std::vector once we get std::span support in C++20
static const std::vector<uint32_t> T6_X_LOCATIONS = {1, 2, 3, 4, 6, 7, 8, 9};
static const std::vector<uint32_t> T6_Y_LOCATIONS = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
static const std::vector<uint32_t> HARVESTING_NOC_LOCATIONS = {11, 1, 10, 2, 9, 3, 8, 4, 7, 5};

static constexpr uint32_t STATIC_TLB_SIZE = 1024 * 1024;  // TODO: Copied from wormhole. Need to verify.

static constexpr xy_pair BROADCAST_LOCATION = {0, 0};  // TODO: Copied from wormhole. Need to verify.
static constexpr uint32_t BROADCAST_TLB_INDEX = 0;     // TODO: Copied from wormhole. Need to verify.
static constexpr uint32_t STATIC_TLB_CFG_ADDR = 0x1fc00000;

static constexpr uint32_t TLB_COUNT_1M = 156;
static constexpr uint32_t TLB_COUNT_2M = 10;
static constexpr uint32_t TLB_COUNT_16M = 20;

static constexpr uint32_t TLB_BASE_1M = 0;
static constexpr uint32_t TLB_BASE_2M = TLB_COUNT_1M * (1 << 20);
static constexpr uint32_t TLB_BASE_16M = TLB_BASE_2M + TLB_COUNT_2M * (1 << 21);

static constexpr uint32_t TLB_BASE_INDEX_1M = 0;
static constexpr uint32_t TLB_BASE_INDEX_2M = TLB_COUNT_1M;
static constexpr uint32_t TLB_BASE_INDEX_16M = TLB_BASE_INDEX_2M + TLB_COUNT_2M;

static constexpr uint32_t DYNAMIC_TLB_16M_SIZE = 16 * 1024 * 1024;
static constexpr uint32_t DYNAMIC_TLB_16M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_16M * 8);
static constexpr uint32_t DYNAMIC_TLB_16M_BASE = TLB_BASE_16M;

static constexpr uint32_t DYNAMIC_TLB_2M_SIZE = 2 * 1024 * 1024;
static constexpr uint32_t DYNAMIC_TLB_2M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_2M * 8);
static constexpr uint32_t DYNAMIC_TLB_2M_BASE = TLB_BASE_2M;

static constexpr uint32_t DYNAMIC_TLB_1M_SIZE = 1 * 1024 * 1024;
static constexpr uint32_t DYNAMIC_TLB_1M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_1M * 8);
static constexpr uint32_t DYNAMIC_TLB_1M_BASE = TLB_BASE_1M;

// REG_TLB for dynamic writes to registers. They are aligned with the kernel driver's WC/UC split.  But kernel driver
// uses different TLB's for these.
static constexpr unsigned int REG_TLB = TLB_BASE_INDEX_16M + 18;

static constexpr unsigned int MEM_LARGE_WRITE_TLB = TLB_BASE_INDEX_16M + 17;
static constexpr unsigned int MEM_LARGE_READ_TLB = TLB_BASE_INDEX_16M + 0;

static constexpr uint32_t DRAM_CHANNEL_0_X = 0;
static constexpr uint32_t DRAM_CHANNEL_0_Y = 0;
static constexpr uint32_t DRAM_CHANNEL_0_PEER2PEER_REGION_START = 0x30000000;  // This is the last 256MB of DRAM

static constexpr uint32_t GRID_SIZE_X = 10;
static constexpr uint32_t GRID_SIZE_Y = 12;

static constexpr uint32_t AXI_RESET_OFFSET = 0x1FF30000;
static constexpr uint32_t ARC_RESET_SCRATCH_OFFSET = AXI_RESET_OFFSET + 0x0060;
static constexpr uint32_t ARC_RESET_ARC_MISC_CNTL_OFFSET = AXI_RESET_OFFSET + 0x0100;

static constexpr uint32_t ARC_CSM_OFFSET = 0x1FE80000;
static constexpr uint32_t ARC_CSM_MAILBOX_OFFSET = ARC_CSM_OFFSET + 0x783C4;
static constexpr uint32_t ARC_CSM_MAILBOX_SIZE_OFFSET = ARC_CSM_OFFSET + 0x784C4;

static constexpr uint32_t ARC_CSM_SPI_TABLE_OFFSET = ARC_CSM_OFFSET + 0x78874;
static constexpr uint32_t ARC_CSM_RowHarvesting_OFFSET = ARC_CSM_OFFSET + 0x78E7C;

static constexpr uint32_t TENSIX_SOFT_RESET_ADDR = 0xFFB121B0;

static constexpr uint32_t MSG_TYPE_SETUP_IATU_FOR_PEER_TO_PEER = 0x97;

static constexpr uint32_t RISCV_RESET_DEASSERT[8] = {0xffffffff, 0xffffffff, 0xffff, 0x0, 0x0, 0x0, 0x0, 0x0};

}  // namespace blackhole

class blackhole_implementation : public architecture_implementation {
   public:
    architecture get_architecture() const override { return architecture::blackhole; }
    uint32_t get_arc_message_arc_get_harvesting() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::ARC_GET_HARVESTING);
    }
    uint32_t get_arc_message_arc_go_busy() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::ARC_GO_BUSY);
    }
    uint32_t get_arc_message_arc_go_long_idle() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::ARC_GO_LONG_IDLE);
    }
    uint32_t get_arc_message_arc_go_short_idle() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::ARC_GO_SHORT_IDLE);
    }
    uint32_t get_arc_message_deassert_riscv_reset() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::DEASSERT_RISCV_RESET);
    }
    uint32_t get_arc_message_get_aiclk() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::GET_AICLK);
    }
    uint32_t get_arc_message_setup_iatu_for_peer_to_peer() const override {
        return static_cast<uint32_t>(blackhole::arc_message_type::SETUP_IATU_FOR_PEER_TO_PEER);
    }
    uint32_t get_arc_message_test() const override { return static_cast<uint32_t>(blackhole::arc_message_type::TEST); }
    uint32_t get_arc_csm_mailbox_offset() const override { return blackhole::ARC_CSM_MAILBOX_OFFSET; }
    uint32_t get_arc_reset_arc_misc_cntl_offset() const override { return blackhole::ARC_RESET_ARC_MISC_CNTL_OFFSET; }
    uint32_t get_arc_reset_scratch_offset() const override { return blackhole::ARC_RESET_SCRATCH_OFFSET; }
    uint32_t get_dram_channel_0_peer2peer_region_start() const override {
        return blackhole::DRAM_CHANNEL_0_PEER2PEER_REGION_START;
    }
    uint32_t get_dram_channel_0_x() const override { return blackhole::DRAM_CHANNEL_0_X; }
    uint32_t get_dram_channel_0_y() const override { return blackhole::DRAM_CHANNEL_0_Y; }
    uint32_t get_broadcast_tlb_index() const override { return blackhole::BROADCAST_TLB_INDEX; }
    uint32_t get_dynamic_tlb_16m_base() const override { return blackhole::DYNAMIC_TLB_16M_BASE; }
    uint32_t get_dynamic_tlb_16m_size() const override { return blackhole::DYNAMIC_TLB_16M_SIZE; }
    uint32_t get_dynamic_tlb_16m_cfg_addr() const override { return blackhole::DYNAMIC_TLB_16M_CFG_ADDR; }
    uint32_t get_mem_large_read_tlb() const override { return blackhole::MEM_LARGE_READ_TLB; }
    uint32_t get_mem_large_write_tlb() const override { return blackhole::MEM_LARGE_WRITE_TLB; }
    uint32_t get_static_tlb_cfg_addr() const override { return blackhole::STATIC_TLB_CFG_ADDR; }
    uint32_t get_static_tlb_size() const override { return blackhole::STATIC_TLB_SIZE; }
    uint32_t get_reg_tlb() const override { return blackhole::REG_TLB; }
    uint32_t get_tlb_base_index_16m() const override { return blackhole::TLB_BASE_INDEX_16M; }
    uint32_t get_tensix_soft_reset_addr() const override { return blackhole::TENSIX_SOFT_RESET_ADDR; }
    uint32_t get_grid_size_x() const override { return blackhole::GRID_SIZE_X; }
    uint32_t get_grid_size_y() const override { return blackhole::GRID_SIZE_Y; }
    const std::vector<uint32_t>& get_harvesting_noc_locations() const override {
        return blackhole::HARVESTING_NOC_LOCATIONS;
    }
    const std::vector<uint32_t>& get_t6_x_locations() const override { return blackhole::T6_X_LOCATIONS; }
    const std::vector<uint32_t>& get_t6_y_locations() const override { return blackhole::T6_Y_LOCATIONS; }

    std::tuple<xy_pair, xy_pair> multicast_workaround(xy_pair start, xy_pair end) const override;
    tlb_configuration get_tlb_configuration(uint32_t tlb_index) const override;
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(std::int32_t tlb_index) const override;
    std::optional<std::uint64_t> get_tlb_data(std::uint32_t tlb_index, const tlb_data& data) const override;
};

}  // namespace tt::umd
