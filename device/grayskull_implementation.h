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

namespace grayskull {

// clang-format off
// See src/t6ifc/t6py/packages/tenstorrent/data/grayskull/pci/tlb.yaml
// 1M
// local_offset: [ 0, 11,  0,  "36-bit address prefix, prepended to the 20 LSBs of issued address to form a 56-bit NOC address. The 1MB TLB #n corresponds to the 1MB MMIO range starting at (0x0 + N*0x100000)."]
// x_end       : [ 0, 17, 12,  "" ]
// y_end       : [ 0, 23, 18,  "" ]
// x_start     : [ 0, 29, 24,  "" ]
// y_start     : [ 0, 35, 30,  "" ]
// noc_sel:      [ 0, 36, 36,  "NOC select (1 = NOC1, 0 = NOC0)"]
// mcast:        [ 0, 37, 37,  "1 = multicast, 0 = unicast"]
// ordering:     [ 0, 39, 38,  "ordering mode (01 = strict (full AXI ordering), 00 = relaxed (no RAW hazard), 10 = posted (may have RAW hazard)"]
// linked:       [ 0, 40, 40,  "linked"]
// clang-format on
static constexpr auto TLB_1M_OFFSET = tlb_offsets{
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

// clang-format off
// 2M
// local_offset: [ 0, 10,  0,  "35-bit address prefix, prepended to the 21 LSBs of issued address to form a 56-bit NOC address. The 2MB TLB #n corresponds to the 2MB MMIO range starting at (0x9C00000 + N*0x200000)."]
// x_end       : [ 0, 16, 11,  "" ]
// y_end       : [ 0, 22, 17,  "" ]
// x_start     : [ 0, 28, 23,  "" ]
// y_start     : [ 0, 34, 29,  "" ]
// noc_sel:      [ 0, 35, 35,  "NOC select (1 = NOC1, 0 = NOC0)"]
// mcast:        [ 0, 36, 36,  "1 = multicast, 0 = unicast"]
// ordering:     [ 0, 38, 37,  "ordering mode (01 = strict (full AXI ordering), 00 = relaxed (no RAW hazard), 10 = posted (may have RAW hazard)"]
// linked:       [ 0, 39, 39,  "linked"]
// clang-format on
static constexpr auto TLB_2M_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 11,
    .y_end = 17,
    .x_start = 23,
    .y_start = 29,
    .noc_sel = 35,
    .mcast = 36,
    .ordering = 37,
    .linked = 39,
    .static_vc = 40,
    .static_vc_end = 41};

// clang-format off
// 16M
// local_offset: [ 0, 7 ,  0,  "32-bit address prefix, prepended to the 24 LSBs of issued address to form a 56-bit NOC address. The 16MB TLB #n corresponds to the 16MB MMIO range starting at (0xB000000 + N*0x1000000)."]
// x_end       : [ 0, 13,  8,  "" ]
// y_end       : [ 0, 19, 14,  "" ]
// x_start     : [ 0, 25, 20,  "" ]
// y_start     : [ 0, 31, 26,  "" ]
// noc_sel:      [ 0, 32, 32,  "NOC select (1 = NOC1, 0 = NOC0)"]
// mcast:        [ 0, 33, 33,  "1 = multicast, 0 = unicast"]
// ordering:     [ 0, 35, 34,  "ordering mode (01 = strict (full AXI ordering), 00 = relaxed (no RAW hazard), 10 = posted (may have RAW hazard)"]
// linked:       [ 0, 36, 36,  "linked"]
// clang-format on
static constexpr auto TLB_16M_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 8,
    .y_end = 14,
    .x_start = 20,
    .y_start = 26,
    .noc_sel = 32,
    .mcast = 33,
    .ordering = 34,
    .linked = 36,
    .static_vc = 37,
    .static_vc_end = 38};

enum class arc_message_type {
    NOP = 0x11,  // Do nothing
    GET_AICLK = 0x34,
    ARC_GO_BUSY = 0x52,
    ARC_GO_SHORT_IDLE = 0x53,
    ARC_GO_LONG_IDLE = 0x54,
    ARC_GET_HARVESTING = 0x57,
    TEST = 0x90,
    NOC_DMA_TRANSFER = 0x9A,
    SETUP_IATU_FOR_PEER_TO_PEER = 0x97,
    DEASSERT_RISCV_RESET = 0xba
};

// DEVICE_DATA
static const std::array<xy_pair, 8> DRAM_LOCATIONS = {{{1, 6}, {4, 6}, {7, 6}, {10, 6}, {1, 0}, {4, 0}, {7, 0}, {10, 0}}};
static const std::array<xy_pair, 1> ARC_LOCATIONS = {{{0, 2}}};
static const std::array<xy_pair, 1> PCI_LOCATIONS = {{{0, 4}}};
static const std::array<xy_pair, 0> ETH_LOCATIONS = {};
// Return to std::array instead of std::vector once we get std::span support in C++20
static const std::vector<uint32_t> T6_X_LOCATIONS = {12, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6};
static const std::vector<uint32_t> T6_Y_LOCATIONS = {11, 1, 10, 2, 9, 3, 8, 4, 7, 5};
static const std::vector<uint32_t> HARVESTING_NOC_LOCATIONS = {5, 7, 4, 8, 3, 9, 2, 10, 1, 11};

static constexpr uint32_t STATIC_TLB_SIZE = 1024 * 1024;

static constexpr xy_pair BROADCAST_LOCATION = {0, 0};
static constexpr uint32_t BROADCAST_TLB_INDEX = 0;

static constexpr uint32_t TLB_COUNT_1M = 156;
static constexpr uint32_t TLB_COUNT_2M = 10;
static constexpr uint32_t TLB_COUNT_16M = 20;

static constexpr uint32_t TLB_BASE_1M = 0;
static constexpr uint32_t TLB_BASE_2M = TLB_COUNT_1M * (1 << 20);
static constexpr uint32_t TLB_BASE_16M = TLB_BASE_2M + TLB_COUNT_2M * (1 << 21);

static constexpr uint32_t TLB_BASE_INDEX_1M = 0;
static constexpr uint32_t TLB_BASE_INDEX_2M = TLB_COUNT_1M;
static constexpr uint32_t TLB_BASE_INDEX_16M = TLB_BASE_INDEX_2M + TLB_COUNT_2M;

static constexpr uint32_t STATIC_TLB_CFG_ADDR = 0x1fc00000;
static constexpr uint32_t TLB_CFG_REG_SIZE_BYTES = 8;

static constexpr uint32_t DYNAMIC_TLB_16M_SIZE = 16 * 1024 * 1024;
static constexpr uint32_t DYNAMIC_TLB_16M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_16M * TLB_CFG_REG_SIZE_BYTES);
static constexpr uint32_t DYNAMIC_TLB_16M_BASE = TLB_BASE_16M;

static constexpr uint32_t DYNAMIC_TLB_2M_SIZE = 2 * 1024 * 1024;
static constexpr uint32_t DYNAMIC_TLB_2M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_2M * TLB_CFG_REG_SIZE_BYTES);
static constexpr uint32_t DYNAMIC_TLB_2M_BASE = TLB_BASE_2M;

static constexpr uint32_t DYNAMIC_TLB_1M_SIZE = 1 * 1024 * 1024;
static constexpr uint32_t DYNAMIC_TLB_1M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_1M * TLB_CFG_REG_SIZE_BYTES);
static constexpr uint32_t DYNAMIC_TLB_1M_BASE = TLB_BASE_1M;

// MEM_*_TLB are for dynamic read/writes to memory, either 16MB (large read/writes) or 2MB (polling). REG_TLB for
// dynamic writes to registers.   They are aligned with the kernel driver's WC/UC split.  But kernel driver uses
// different TLB's for these.
static constexpr unsigned int REG_TLB = TLB_BASE_INDEX_16M + 18;
static constexpr unsigned int MEM_LARGE_WRITE_TLB = TLB_BASE_INDEX_16M + 17;
static constexpr unsigned int MEM_LARGE_READ_TLB = TLB_BASE_INDEX_16M + 0;
static constexpr unsigned int MEM_SMALL_READ_WRITE_TLB = TLB_BASE_INDEX_2M + 1;

static constexpr uint32_t DRAM_CHANNEL_0_X = 1;
static constexpr uint32_t DRAM_CHANNEL_0_Y = 0;
static constexpr uint32_t DRAM_CHANNEL_0_PEER2PEER_REGION_START = 0x30000000;  // This is the last 256MB of DRAM

static constexpr uint32_t GRID_SIZE_X = 13;
static constexpr uint32_t GRID_SIZE_Y = 12;

static constexpr uint32_t ARC_RESET_SCRATCH_OFFSET = 0x1FF30060;
static constexpr uint32_t ARC_RESET_ARC_MISC_CNTL_OFFSET = 0x1FF30100;

static constexpr uint32_t ARC_CSM_MAILBOX_OFFSET = 0x1FEF83BC;
static constexpr uint32_t ARC_CSM_MAILBOX_SIZE_OFFSET = 0x1FEF84BC;

static constexpr uint32_t TENSIX_SOFT_RESET_ADDR = 0xFFB121B0;

}  // namespace grayskull

class grayskull_implementation : public architecture_implementation {
   public:
    architecture get_architecture() const override { return architecture::grayskull; }
    uint32_t get_arc_message_arc_get_harvesting() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::ARC_GET_HARVESTING);
    }
    uint32_t get_arc_message_arc_go_busy() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::ARC_GO_BUSY);
    }
    uint32_t get_arc_message_arc_go_long_idle() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::ARC_GO_LONG_IDLE);
    }
    uint32_t get_arc_message_arc_go_short_idle() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::ARC_GO_SHORT_IDLE);
    }
    uint32_t get_arc_message_deassert_riscv_reset() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::DEASSERT_RISCV_RESET);
    }
    uint32_t get_arc_message_get_aiclk() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::GET_AICLK);
    }
    uint32_t get_arc_message_setup_iatu_for_peer_to_peer() const override {
        return static_cast<uint32_t>(grayskull::arc_message_type::SETUP_IATU_FOR_PEER_TO_PEER);
    }
    uint32_t get_arc_message_test() const override { return static_cast<uint32_t>(grayskull::arc_message_type::TEST); }
    uint32_t get_arc_csm_mailbox_offset() const override { return grayskull::ARC_CSM_MAILBOX_OFFSET; }
    uint32_t get_arc_reset_arc_misc_cntl_offset() const override { return grayskull::ARC_RESET_ARC_MISC_CNTL_OFFSET; }
    uint32_t get_arc_reset_scratch_offset() const override { return grayskull::ARC_RESET_SCRATCH_OFFSET; }
    uint32_t get_dram_channel_0_peer2peer_region_start() const override {
        return grayskull::DRAM_CHANNEL_0_PEER2PEER_REGION_START;
    }
    uint32_t get_dram_channel_0_x() const override { return grayskull::DRAM_CHANNEL_0_X; }
    uint32_t get_dram_channel_0_y() const override { return grayskull::DRAM_CHANNEL_0_Y; }
    uint32_t get_broadcast_tlb_index() const override { return grayskull::BROADCAST_TLB_INDEX; }
    uint32_t get_dynamic_tlb_2m_base() const override { return grayskull::DYNAMIC_TLB_2M_BASE; }
    uint32_t get_dynamic_tlb_2m_size() const override { return grayskull::DYNAMIC_TLB_2M_SIZE; }
    uint32_t get_dynamic_tlb_16m_base() const override { return grayskull::DYNAMIC_TLB_16M_BASE; }
    uint32_t get_dynamic_tlb_16m_size() const override { return grayskull::DYNAMIC_TLB_16M_SIZE; }
    uint32_t get_dynamic_tlb_16m_cfg_addr() const override { return grayskull::DYNAMIC_TLB_16M_CFG_ADDR; }
    uint32_t get_mem_large_read_tlb() const override { return grayskull::MEM_LARGE_READ_TLB; }
    uint32_t get_mem_large_write_tlb() const override { return grayskull::MEM_LARGE_WRITE_TLB; }
    uint32_t get_static_tlb_cfg_addr() const override { return grayskull::STATIC_TLB_CFG_ADDR; }
    uint32_t get_static_tlb_size() const override { return grayskull::STATIC_TLB_SIZE; }
    uint32_t get_reg_tlb() const override { return grayskull::REG_TLB; }
    uint32_t get_tlb_base_index_16m() const override { return grayskull::TLB_BASE_INDEX_16M; }
    uint32_t get_tensix_soft_reset_addr() const override { return grayskull::TENSIX_SOFT_RESET_ADDR; }
    uint32_t get_grid_size_x() const override { return grayskull::GRID_SIZE_X; }
    uint32_t get_grid_size_y() const override { return grayskull::GRID_SIZE_Y; }
    uint32_t get_tlb_cfg_reg_size_bytes() const override { return grayskull::TLB_CFG_REG_SIZE_BYTES; }
    const std::vector<uint32_t>& get_harvesting_noc_locations() const override {
        return grayskull::HARVESTING_NOC_LOCATIONS;
    }
    const std::vector<uint32_t>& get_t6_x_locations() const override { return grayskull::T6_X_LOCATIONS; }
    const std::vector<uint32_t>& get_t6_y_locations() const override { return grayskull::T6_Y_LOCATIONS; }

    std::tuple<xy_pair, xy_pair> multicast_workaround(xy_pair start, xy_pair end) const override;
    tlb_configuration get_tlb_configuration(uint32_t tlb_index) const override;
    std::optional<std::tuple<std::uint32_t, std::uint32_t>> describe_tlb(std::int32_t tlb_index) const override;
    std::pair<std::uint64_t, std::uint64_t> get_tlb_data(std::uint32_t tlb_index, const tlb_data& data) const override;
};

}  // namespace tt::umd
