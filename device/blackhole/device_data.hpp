#pragma once

#include "device/tt_xy_pair.h"
#include <cstdint>
#include <vector>
#include <cassert>
#include <algorithm>

#ifdef ARCH_GRAYSKULL
#error "CANNOT INCLUDE GRAYSKULL AND BLACKHOLE."
#elifdef ARCH_WORMHOLE
#error "CANNOT INCLUDE WORMHOLE AND BLACKHOLE."
#endif
#define ARCH_BLACKHOLE

typedef enum {
    NOP                             = 0x11,   // Do nothing
    GET_AICLK                       = 0x34,
    ARC_GO_BUSY                     = 0x52,
    ARC_GO_SHORT_IDLE               = 0x53,
    ARC_GO_LONG_IDLE                = 0x54,
    ARC_GET_HARVESTING              = 0x57,
    SET_ETH_DRAM_TRAINED_STATUS     = 0x58,
    TEST                            = 0x90,
    SETUP_IATU_FOR_PEER_TO_PEER     = 0x97,
    DEASSERT_RISCV_RESET            = 0xba
} MSG_TYPE;

struct BLACKHOLE_DEVICE_DATA {
    const std::vector<tt_xy_pair> DRAM_LOCATIONS = {
        {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {0, 8}, {0, 9}, {0, 10}, {0, 11}, {9, 9}, {9, 1}, {9, 2}, {9, 3}, {9, 4}, {9, 5}, {9, 6}, {9, 7}, {9, 8}, {9, 9}, {9, 10}, {9, 11}
    };
    const std::vector<tt_xy_pair> ARC_LOCATIONS = { {8, 0} };
    const std::vector<tt_xy_pair> PCI_LOCATIONS = { {11, 0} };
    const std::vector<tt_xy_pair> ETH_LOCATIONS = {
        // Add ethernet locations later
    };
    const std::vector<uint32_t> T6_X_LOCATIONS = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16};
    const std::vector<uint32_t> T6_Y_LOCATIONS = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const std::vector<uint32_t> HARVESTING_NOC_LOCATIONS = {11, 1, 10, 2, 9, 3, 8, 4, 7, 5};

    static constexpr uint32_t STATIC_TLB_CFG_ADDR = 0x1fc00000;

    static constexpr uint32_t TLB_COUNT_2M = 202;
    static constexpr uint32_t TLB_BASE_2M = 0;
    static constexpr uint32_t TLB_BASE_INDEX_2M = 0;
    static constexpr uint32_t TLB_2M_SIZE = 2 * 1024 * 1024;

    static constexpr uint32_t TLB_2M_CFG_ADDR_SIZE = 12;
    static constexpr uint32_t DYNAMIC_TLB_2M_SIZE = 2 * 1024 * 1024;
    static constexpr uint32_t DYNAMIC_TLB_2M_CFG_ADDR = STATIC_TLB_CFG_ADDR + (TLB_BASE_INDEX_2M * TLB_2M_CFG_ADDR_SIZE);
    static constexpr uint32_t DYNAMIC_TLB_2M_BASE = TLB_BASE_2M;

    // REG_TLB for dynamic writes to registers. They are aligned with the kernel driver's WC/UC split.  But kernel driver uses different TLB's for these.
    // Revisit for BH
    static constexpr unsigned int REG_TLB = TLB_BASE_2M + 0;

    static constexpr unsigned int MEM_LARGE_WRITE_TLB = TLB_BASE_2M + 0;
    static constexpr unsigned int MEM_LARGE_READ_TLB = TLB_BASE_2M + 0;

    static constexpr uint32_t DRAM_CHANNEL_0_X = 0;
    static constexpr uint32_t DRAM_CHANNEL_0_Y = 0;
    static constexpr uint32_t DRAM_CHANNEL_0_PEER2PEER_REGION_START = 0x30000000; // This is the last 256MB of DRAM

    static constexpr uint32_t GRID_SIZE_X = 17;
    static constexpr uint32_t GRID_SIZE_Y = 12;

    // AXI Resets accessed through TLB
    static constexpr uint32_t TENSIX_SM_TLB_INDEX = 188;
    static constexpr uint32_t AXI_RESET_OFFSET = TLB_BASE_2M + TENSIX_SM_TLB_INDEX * TLB_2M_SIZE;
    static constexpr uint32_t ARC_RESET_SCRATCH_OFFSET = AXI_RESET_OFFSET + 0x0060;
    static constexpr uint32_t ARC_RESET_ARC_MISC_CNTL_OFFSET = AXI_RESET_OFFSET + 0x0100;

    // MT: This is no longer valid for Blackhole. Review messages to ARC
    static constexpr uint32_t ARC_CSM_OFFSET = 0x1FE80000;
    static constexpr uint32_t ARC_CSM_MAILBOX_OFFSET = ARC_CSM_OFFSET + 0x783C4;
    static constexpr uint32_t ARC_CSM_MAILBOX_SIZE_OFFSET = ARC_CSM_OFFSET + 0x784C4;

    static constexpr uint32_t TENSIX_SOFT_RESET_ADDR = 0xFFB121B0;

    static constexpr uint32_t MSG_TYPE_SETUP_IATU_FOR_PEER_TO_PEER = 0x97;

    static constexpr uint32_t RISCV_RESET_DEASSERT[8] = { 0xffffffff, 0xffffffff, 0xffff, 0x0, 0x0, 0x0, 0x0, 0x0 };
};

static const auto DEVICE_DATA = BLACKHOLE_DEVICE_DATA();
