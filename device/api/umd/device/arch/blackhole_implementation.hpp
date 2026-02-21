// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <stdexcept>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/common.hpp"

namespace tt::umd {

namespace blackhole {

inline constexpr auto TLB_2M_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 43,
    .y_end = 49,
    .x_start = 55,
    .y_start = 61,
    .noc_sel = 67,
    .mcast = 69,
    .ordering = 70,
    .linked = 72,
    .static_vc = 73,
    // missing .stream_header
    .static_vc_end = 75};

inline constexpr auto TLB_4G_OFFSET = tlb_offsets{
    .local_offset = 0,
    .x_end = 32,
    .y_end = 38,
    .x_start = 44,
    .y_start = 50,
    .noc_sel = 56,
    .mcast = 58,
    .ordering = 59,
    .linked = 61,
    .static_vc = 62,
    // missing .stream_header
    .static_vc_end = 64};

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

// DEVICE_DATA.
inline constexpr tt_xy_pair GRID_SIZE = {17, 12};
// Vectors for mapping NOC0 x and y coordinates to NOC1 x and y coordinates.
// NOC0_X_TO_NOC1_X[noc0_x] is the NOC1 x coordinate corresponding to NOC0 x coordinate noc0_x.
// NOC0_Y_TO_NOC1_Y[noc0_y] is the NOC1 y coordinate corresponding to NOC0 y coordinate noc0_y.
static const std::vector<uint32_t> NOC0_X_TO_NOC1_X = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const std::vector<uint32_t> NOC0_Y_TO_NOC1_Y = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
const static tt_xy_pair TENSIX_GRID_SIZE = {14, 10};
// clang-format off
const static std::vector<tt_xy_pair> TENSIX_CORES_NOC0 = {
    {1, 2},   {2, 2},  {3, 2},  {4, 2},  {5, 2},  {6, 2},  {7, 2},  {10, 2},  {11, 2},  {12, 2},  {13, 2},  {14, 2},  {15, 2},  {16, 2},
    {1, 3},   {2, 3},  {3, 3},  {4, 3},  {5, 3},  {6, 3},  {7, 3},  {10, 3},  {11, 3},  {12, 3},  {13, 3},  {14, 3},  {15, 3},  {16, 3},
    {1, 4},   {2, 4},  {3, 4},  {4, 4},  {5, 4},  {6, 4},  {7, 4},  {10, 4},  {11, 4},  {12, 4},  {13, 4},  {14, 4},  {15, 4},  {16, 4},
    {1, 5},   {2, 5},  {3, 5},  {4, 5},  {5, 5},  {6, 5},  {7, 5},  {10, 5},  {11, 5},  {12, 5},  {13, 5},  {14, 5},  {15, 5},  {16, 5},
    {1, 6},   {2, 6},  {3, 6},  {4, 6},  {5, 6},  {6, 6},  {7, 6},  {10, 6},  {11, 6},  {12, 6},  {13, 6},  {14, 6},  {15, 6},  {16, 6},
    {1, 7},   {2, 7},  {3, 7},  {4, 7},  {5, 7},  {6, 7},  {7, 7},  {10, 7},  {11, 7},  {12, 7},  {13, 7},  {14, 7},  {15, 7},  {16, 7},
    {1, 8},   {2, 8},  {3, 8},  {4, 8},  {5, 8},  {6, 8},  {7, 8},  {10, 8},  {11, 8},  {12, 8},  {13, 8},  {14, 8},  {15, 8},  {16, 8},
    {1, 9},   {2, 9},  {3, 9},  {4, 9},  {5, 9},  {6, 9},  {7, 9},  {10, 9},  {11, 9},  {12, 9},  {13, 9},  {14, 9},  {15, 9},  {16, 9},
    {1, 10}, {2, 10}, {3, 10}, {4, 10}, {5, 10}, {6, 10}, {7, 10}, {10, 10}, {11, 10}, {12, 10}, {13, 10}, {14, 10}, {15, 10}, {16, 10},
    {1, 11}, {2, 11}, {3, 11}, {4, 11}, {5, 11}, {6, 11}, {7, 11}, {10, 11}, {11, 11}, {12, 11}, {13, 11}, {14, 11}, {15, 11}, {16, 11},
};
// clang-format on

inline constexpr std::size_t NUM_DRAM_BANKS = 8;
inline constexpr std::size_t NUM_NOC_PORTS_PER_DRAM_BANK = 3;
inline constexpr tt_xy_pair DRAM_GRID_SIZE = {NUM_DRAM_BANKS, NUM_NOC_PORTS_PER_DRAM_BANK};
// clang-format off
static const std::vector<std::vector<tt_xy_pair>> DRAM_CORES_NOC0 = {
    {{0, 0},  {0, 1}, {0, 11}},
    {{0, 2}, {0, 10},  {0, 3}},
    {{0, 9},  {0, 4},  {0, 8}},
    {{0, 5},  {0, 7},  {0, 6}},
    {{9, 0},  {9, 1}, {9, 11}},
    {{9, 2}, {9, 10},  {9, 3}},
    {{9, 9},  {9, 4},  {9, 8}},
    {{9, 5},  {9, 7},  {9, 6}}};
// clang-format on
// TODO: DRAM locations should be deleted. We keep it for compatibility with
// the existing code in clients which rely on DRAM_LOCATIONS.
static const std::vector<tt_xy_pair> DRAM_LOCATIONS = flatten_vector(DRAM_CORES_NOC0);

inline constexpr tt_xy_pair ARC_GRID_SIZE = {1, 1};
static const std::vector<tt_xy_pair> ARC_CORES_NOC0 = {{8, 0}};
static const std::vector<tt_xy_pair> ARC_LOCATIONS = ARC_CORES_NOC0;

inline constexpr tt_xy_pair PCIE_GRID_SIZE = {2, 1};
static const std::vector<tt_xy_pair> PCIE_CORES_TYPE2_NOC0 = {{{2, 0}}};
static const std::vector<tt_xy_pair> PCI_LOCATIONS = PCIE_CORES_TYPE2_NOC0;
static const std::vector<tt_xy_pair> PCIE_CORES_TYPE1_NOC0 = {{{11, 0}}};
static const std::vector<tt_xy_pair> PCIE_CORES_NOC0 = {{2, 0}, {11, 0}};

static const std::vector<tt_xy_pair> ROUTER_CORES_NOC0 = {
    {1, 0},
    {3, 0},
    {4, 0},
    {5, 0},
    {6, 0},
    {7, 0},
    {10, 0},
    {12, 0},
    {13, 0},
    {14, 0},
    {15, 0},
    {16, 0},
    {8, 1},
    {8, 10},
    {8, 8},
    {8, 6},
    {8, 4},
    {8, 11}};

inline constexpr size_t NUM_ETH_CHANNELS = 14;
static const std::vector<tt_xy_pair> ETH_CORES_NOC0 = {
    {{1, 1},
     {16, 1},
     {2, 1},
     {15, 1},
     {3, 1},
     {14, 1},
     {4, 1},
     {13, 1},
     {5, 1},
     {12, 1},
     {6, 1},
     {11, 1},
     {7, 1},
     {10, 1}}};

static const std::vector<tt_xy_pair> ETH_LOCATIONS = ETH_CORES_NOC0;

static const std::vector<tt_xy_pair> SECURITY_CORES_NOC0 = {{8, 2}};
// We are using P0 on the NOC for all L2CPU cores.
static const std::vector<tt_xy_pair> L2CPU_CORES_NOC0 = {{8, 3}, {8, 5}, {8, 7}, {8, 9}};

// Return to std::array instead of std::vector once we get std::span support in C++20.
static const std::vector<uint32_t> T6_X_LOCATIONS = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16};
static const std::vector<uint32_t> T6_Y_LOCATIONS = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const std::vector<uint32_t> HARVESTING_NOC_LOCATIONS = {1, 16, 2, 15, 3, 14, 4, 13, 5, 12, 6, 11, 7, 10};
static const std::vector<uint32_t> LOGICAL_HARVESTING_LAYOUT = {0, 2, 4, 6, 8, 10, 12, 13, 11, 9, 7, 5, 3, 1};

inline constexpr uint32_t STATIC_TLB_SIZE = 2 * 1024 * 1024;  // 2MB

inline constexpr xy_pair BROADCAST_LOCATION = {0, 0};  // TODO: Copied from wormhole. Need to verify.
inline constexpr uint32_t BROADCAST_TLB_INDEX = 0;     // TODO: Copied from wormhole. Need to verify.
inline constexpr uint32_t STATIC_TLB_CFG_ADDR = 0x1fc00000;

inline constexpr uint32_t TLB_COUNT_2M = 202;
inline constexpr uint32_t TLB_BASE_2M = 0;  // 0 in BAR0
inline constexpr uint32_t TLB_BASE_INDEX_2M = 0;
inline constexpr uint32_t TLB_2M_SIZE = 2 * 1024 * 1024;

inline constexpr uint32_t TLB_CFG_REG_SIZE_BYTES = 12;

inline constexpr uint32_t TLB_COUNT_4G = 8;
inline constexpr uint32_t TLB_BASE_4G = 0;  // 0 in BAR4
inline constexpr uint32_t TLB_BASE_INDEX_4G = TLB_COUNT_2M;
inline constexpr uint64_t TLB_4G_SIZE = 4ULL * 1024ULL * 1024ULL * 1024ULL;
inline constexpr uint64_t DYNAMIC_TLB_4G_SIZE = TLB_4G_SIZE;
inline constexpr uint32_t DYNAMIC_TLB_4G_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_4G * TLB_CFG_REG_SIZE_BYTES);
inline constexpr uint32_t DYNAMIC_TLB_4G_BASE = TLB_BASE_4G;

inline constexpr uint32_t DYNAMIC_TLB_COUNT = 16;

inline constexpr uint32_t DYNAMIC_TLB_2M_SIZE = 2 * 1024 * 1024;
inline constexpr uint32_t DYNAMIC_TLB_2M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_2M * TLB_CFG_REG_SIZE_BYTES);
inline constexpr uint32_t DYNAMIC_TLB_2M_BASE = TLB_BASE_2M;

// REG_TLB for dynamic writes to registers. They are aligned with the kernel driver's WC/UC split.  But kernel driver
// uses different TLB's for these.
// Revisit for BH.
inline constexpr unsigned int REG_TLB = TLB_BASE_INDEX_2M + 191;

inline constexpr uint32_t DYNAMIC_TLB_BASE_INDEX = TLB_BASE_INDEX_2M + 180;
inline constexpr unsigned int MEM_LARGE_WRITE_TLB = TLB_BASE_INDEX_2M + 181;
inline constexpr unsigned int MEM_LARGE_READ_TLB = TLB_BASE_INDEX_2M + 182;
inline constexpr unsigned int MEM_SMALL_READ_WRITE_TLB = TLB_BASE_INDEX_2M + 183;

inline constexpr uint32_t DRAM_CHANNEL_0_X = 0;
inline constexpr uint32_t DRAM_CHANNEL_0_Y = 1;
inline constexpr uint32_t DRAM_CHANNEL_0_PEER2PEER_REGION_START = 0x30000000;  // This is the last 256MB of DRAM

inline constexpr uint32_t GRID_SIZE_X = 17;
inline constexpr uint32_t GRID_SIZE_Y = 12;

// AXI Resets accessed through TLB.
inline constexpr uint32_t TENSIX_SM_TLB_INDEX = 188;
inline constexpr uint32_t AXI_RESET_OFFSET = TLB_BASE_2M + TENSIX_SM_TLB_INDEX * TLB_2M_SIZE;
inline constexpr uint32_t ARC_RESET_ARC_MISC_CNTL_OFFSET = AXI_RESET_OFFSET + 0x0100;

// Computed this value from AXI_RESET_OFFSET.
inline constexpr uint32_t ARC_APB_BAR0_XBAR_OFFSET_START = 0x1FF00000;

// MT: This is no longer valid for Blackhole. Review messages to ARC.
inline constexpr uint32_t ARC_CSM_OFFSET = 0x1FE80000;
inline constexpr uint32_t ARC_CSM_MAILBOX_OFFSET = ARC_CSM_OFFSET + 0x783C4;
inline constexpr uint32_t ARC_CSM_MAILBOX_SIZE_OFFSET = ARC_CSM_OFFSET + 0x784C4;

inline constexpr uint32_t TENSIX_SOFT_RESET_ADDR = 0xFFB121B0;

inline constexpr uint32_t RISCV_DEBUG_REG_DBG_BUS_CNTL_REG = 0xFFB12000 + 0x54;

inline constexpr uint32_t MSG_TYPE_SETUP_IATU_FOR_PEER_TO_PEER = 0x97;

static const uint32_t BH_NOC_NODE_ID_OFFSET = 0x1FD04044;

inline constexpr uint32_t ARC_XBAR_ADDRESS_END = 0xFFFFFFFF;

inline constexpr uint64_t ARC_NOC_XBAR_ADDRESS_START = 0x80000000;
inline constexpr uint64_t ARC_NOC_TO_ARC_XBAR_MAP_ADDRESS_START = 0x800000000;

inline constexpr uint32_t ARC_RESET_UNIT_OFFSET = 0x30000;
inline constexpr uint32_t ARC_RESET_SCRATCH_OFFSET = ARC_RESET_UNIT_OFFSET + 0x0060;
inline constexpr uint32_t ARC_RESET_SCRATCH_2_OFFSET = ARC_RESET_SCRATCH_OFFSET + 0x8;
inline constexpr uint32_t ARC_RESET_REFCLK_LOW_OFFSET = ARC_RESET_UNIT_OFFSET + 0xE0;
inline constexpr uint32_t ARC_RESET_REFCLK_HIGH_OFFSET = ARC_RESET_UNIT_OFFSET + 0xE4;

// Register from which address of the ARC queue control block is read.
inline constexpr uint32_t SCRATCH_RAM_11 = ARC_RESET_UNIT_OFFSET + 0x42C;

// ARC message queue header and entry size in bytes.
inline constexpr uint32_t ARC_MSG_QUEUE_HEADER_SIZE = 32;
inline constexpr uint32_t ARC_QUEUE_ENTRY_SIZE = 32;

// ARC firmware interrupt address and value to write in order
// to make an interrupt request.
inline constexpr uint32_t ARC_FW_INT_ADDR = ARC_RESET_UNIT_OFFSET + 0x100;
inline constexpr uint32_t ARC_FW_INT_VAL = 65536;

inline constexpr uint32_t ARC_MSG_RESPONSE_OK_LIMIT = 240;

inline constexpr uint32_t SCRATCH_RAM_0 = ARC_RESET_UNIT_OFFSET + 0x400;
inline constexpr uint32_t SCRATCH_RAM_2 = ARC_RESET_UNIT_OFFSET + 0x408;
inline constexpr uint32_t SCRATCH_RAM_10 = ARC_RESET_UNIT_OFFSET + 0x428;  // SPI buffer info
inline constexpr uint32_t SCRATCH_RAM_12 = ARC_RESET_UNIT_OFFSET + 0x430;
inline constexpr uint32_t SCRATCH_RAM_13 = ARC_RESET_UNIT_OFFSET + 0x434;

inline constexpr uint32_t NIU_CFG_NOC0_BAR_ADDR = 0x1FD04100;
inline constexpr uint32_t NIU_CFG_NOC1_BAR_ADDR = 0x1FD14100;

inline constexpr uint64_t NIU_CFG_NOC0_ARC_ADDR = 0x80050100;
inline constexpr uint64_t NIU_CFG_NOC1_ARC_ADDR = 0x80058100;

inline constexpr uint32_t AICLK_BUSY_VAL = 1350;
inline constexpr uint32_t AICLK_IDLE_VAL = 800;

inline constexpr uint32_t TENSIX_L1_SIZE = 1572864;
inline constexpr uint32_t ETH_L1_SIZE = 262144;
inline constexpr uint64_t DRAM_BANK_SIZE = 4294967296;

inline constexpr std::array<std::pair<CoreType, uint64_t>, 8> NOC0_CONTROL_REG_ADDR_BASE_MAP = {
    {{CoreType::TENSIX, 0xFFB20000},
     {CoreType::ETH, 0xFFB20000},
     {CoreType::DRAM, 0xFFB20000},
     {CoreType::PCIE, 0xFFFFFFFFFF000000ULL},
     {CoreType::ARC, 0xFFFFFFFFFF000000ULL},
     {CoreType::SECURITY, 0xFFFFFFFFFF000000ULL},
     {CoreType::L2CPU, 0xFFFFFFFFFF000000ULL},
     {CoreType::ROUTER_ONLY, 0xFF000000}}};
inline constexpr std::array<std::pair<CoreType, uint64_t>, 8> NOC1_CONTROL_REG_ADDR_BASE_MAP = {
    {{CoreType::TENSIX, 0xFFB30000},
     {CoreType::ETH, 0xFFB30000},
     {CoreType::DRAM, 0xFFB30000},
     {CoreType::PCIE, 0xFFFFFFFFFF000000ULL},
     {CoreType::ARC, 0xFFFFFFFFFF000000ULL},
     {CoreType::SECURITY, 0xFFFFFFFFFF000000ULL},
     {CoreType::L2CPU, 0xFFFFFFFFFF000000ULL},
     {CoreType::ROUTER_ONLY, 0xFF000000}}};

inline constexpr uint64_t NOC_NODE_ID_OFFSET = 0x44;
inline constexpr uint64_t NOC_ID_LOGICAL_OFFSET = 0x148;

inline constexpr size_t eth_translated_coordinate_start_x = 20;
inline constexpr size_t eth_translated_coordinate_start_y = 25;

inline constexpr size_t pcie_translated_coordinate_start_x = 19;
inline constexpr size_t pcie_translated_coordinate_start_y = 24;

inline constexpr size_t dram_translated_coordinate_start_x = 17;
inline constexpr size_t dram_translated_coordinate_start_y = 12;

// Constants related to bits in the soft reset register.
inline constexpr uint32_t SOFT_RESET_BRISC = 1 << 11;
inline constexpr uint32_t SOFT_RESET_TRISC0 = 1 << 12;
inline constexpr uint32_t SOFT_RESET_TRISC1 = 1 << 13;
inline constexpr uint32_t SOFT_RESET_TRISC2 = 1 << 14;
inline constexpr uint32_t SOFT_RESET_NCRISC = 1 << 18;
inline constexpr uint32_t SOFT_RESET_STAGGERED_START = 1 << 31;

// Return arc core pair that can be used to access ARC core on the device. This depends on information
// whether NOC translation is enabled and if we want to use NOC0 or NOC1.
tt_xy_pair get_arc_core(const bool noc_translation_enabled, const bool use_noc1);

}  // namespace blackhole

class blackhole_implementation : public architecture_implementation {
public:
    tt::ARCH get_architecture() const override { return tt::ARCH::BLACKHOLE; }

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

    uint32_t get_arc_csm_bar0_mailbox_offset() const override {
        throw std::runtime_error("Not implemented for Blackhole arch");
    }

    uint32_t get_arc_axi_apb_peripheral_offset() const override { return blackhole::ARC_APB_BAR0_XBAR_OFFSET_START; }

    uint32_t get_arc_reset_arc_misc_cntl_offset() const override { return blackhole::ARC_RESET_ARC_MISC_CNTL_OFFSET; }

    uint32_t get_arc_reset_scratch_offset() const override { return blackhole::ARC_RESET_SCRATCH_OFFSET; }

    uint32_t get_arc_reset_scratch_2_offset() const override { return blackhole::ARC_RESET_SCRATCH_2_OFFSET; }

    uint32_t get_arc_reset_unit_refclk_low_offset() const override { return blackhole::ARC_RESET_REFCLK_LOW_OFFSET; }

    uint32_t get_arc_reset_unit_refclk_high_offset() const override { return blackhole::ARC_RESET_REFCLK_HIGH_OFFSET; }

    uint32_t get_dram_channel_0_peer2peer_region_start() const override {
        return blackhole::DRAM_CHANNEL_0_PEER2PEER_REGION_START;
    }

    uint32_t get_dram_channel_0_x() const override { return blackhole::DRAM_CHANNEL_0_X; }

    uint32_t get_dram_channel_0_y() const override { return blackhole::DRAM_CHANNEL_0_Y; }

    uint32_t get_dram_banks_number() const override { return blackhole::NUM_DRAM_BANKS; }

    uint32_t get_broadcast_tlb_index() const override { return blackhole::BROADCAST_TLB_INDEX; }

    uint32_t get_dynamic_tlb_2m_base() const override { return blackhole::DYNAMIC_TLB_2M_BASE; }

    uint32_t get_dynamic_tlb_2m_size() const override { return blackhole::DYNAMIC_TLB_2M_SIZE; }

    uint32_t get_dynamic_tlb_16m_base() const override {
        throw std::runtime_error("No 16MB TLBs for Blackhole arch");
        return 0;
    }

    uint32_t get_dynamic_tlb_16m_size() const override {
        throw std::runtime_error("No 16MB TLBs for Blackhole arch");
        return 0;
    }

    uint32_t get_dynamic_tlb_16m_cfg_addr() const override {
        throw std::runtime_error("No 16MB TLBs for Blackhole arch");
        return 0;
    }

    uint32_t get_mem_large_read_tlb() const override { return blackhole::MEM_LARGE_READ_TLB; }

    uint32_t get_mem_large_write_tlb() const override { return blackhole::MEM_LARGE_WRITE_TLB; }

    uint32_t get_num_eth_channels() const override { return blackhole::NUM_ETH_CHANNELS; }

    uint32_t get_read_checking_offset() const override { return blackhole::BH_NOC_NODE_ID_OFFSET; }

    uint32_t get_reg_tlb() const override { return blackhole::REG_TLB; }

    uint32_t get_tlb_base_index_16m() const override {
        throw std::runtime_error("No 16MB TLBs for Blackhole arch");
        return 0;
    }

    uint32_t get_tensix_soft_reset_addr() const override { return blackhole::TENSIX_SOFT_RESET_ADDR; }

    uint32_t get_debug_reg_addr() const override { return blackhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG; }

    uint32_t get_soft_reset_reg_value(RiscType risc_type) const override;

    RiscType get_soft_reset_risc_type(uint32_t soft_reset_reg_value) const override;

    uint32_t get_soft_reset_staggered_start() const override { return blackhole::SOFT_RESET_STAGGERED_START; }

    uint32_t get_grid_size_x() const override { return blackhole::GRID_SIZE_X; }

    uint32_t get_grid_size_y() const override { return blackhole::GRID_SIZE_Y; }

    uint64_t get_arc_apb_noc_base_address() const override { return blackhole::ARC_NOC_XBAR_ADDRESS_START; }

    uint64_t get_arc_csm_noc_base_address() const override {
        throw std::runtime_error("CSM fetch base address not implemented for Blackhole.");
    }

    const std::vector<uint32_t>& get_harvesting_noc_locations() const override {
        return blackhole::HARVESTING_NOC_LOCATIONS;
    }

    const std::vector<uint32_t>& get_t6_x_locations() const override { return blackhole::T6_X_LOCATIONS; }

    const std::vector<uint32_t>& get_t6_y_locations() const override { return blackhole::T6_Y_LOCATIONS; }

    const std::vector<std::vector<tt_xy_pair>>& get_dram_cores_noc0() const override {
        return blackhole::DRAM_CORES_NOC0;
    };

    std::pair<uint32_t, uint32_t> get_tlb_1m_base_and_count() const override { return {0, 0}; }

    std::pair<uint32_t, uint32_t> get_tlb_2m_base_and_count() const override {
        return {blackhole::TLB_BASE_2M, blackhole::TLB_COUNT_2M};
    }

    std::pair<uint32_t, uint32_t> get_tlb_16m_base_and_count() const override { return {0, 0}; }

    std::pair<uint32_t, uint32_t> get_tlb_4g_base_and_count() const override {
        return {blackhole::TLB_BASE_4G, blackhole::TLB_COUNT_4G};
    }

    const std::vector<size_t>& get_tlb_sizes() const override {
        static constexpr uint32_t one_mb = 1 << 20;
        static constexpr size_t one_gb = 1024ULL * one_mb;
        static const std::vector<size_t> tlb_sizes = {2 * one_mb, 4ULL * one_gb};
        return tlb_sizes;
    }

    std::tuple<xy_pair, xy_pair> multicast_workaround(xy_pair start, xy_pair end) const override;
    tlb_configuration get_tlb_configuration(uint32_t tlb_index) const override;

    DeviceL1AddressParams get_l1_address_params() const override;
    DriverHostAddressParams get_host_address_params() const override;
    DriverEthInterfaceParams get_eth_interface_params() const override;
    DriverNocParams get_noc_params() const override;

    uint64_t get_noc_node_id_offset() const override { return blackhole::NOC_NODE_ID_OFFSET; }

    uint64_t get_noc_node_translated_id_offset() const override { return blackhole::NOC_ID_LOGICAL_OFFSET; }

    uint64_t get_noc_reg_base(const CoreType core_type, const uint32_t noc, const uint32_t noc_port = 0) const override;

    size_t get_cached_tlb_size() const override { return blackhole::STATIC_TLB_SIZE; }

    bool get_static_vc() const override { return false; }  // False due to a known HW issue.
};

}  // namespace tt::umd
