// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arch/architecture_registers.hpp"

#include <fmt/format.h>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

namespace wormhole {

static uint64_t noc_reg_base(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    if (noc == 0) {
        for (const auto& noc_pair : NOC0_CONTROL_REG_ADDR_BASE_MAP) {
            if (core_type == CoreType::DRAM) {
                return DRAM_NOC0_CONTROL_REG_ADDR_BASE_MAP[noc_port];
            }
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    } else if (noc == 1) {
        for (const auto& noc_pair : NOC1_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                if (core_type == CoreType::DRAM) {
                    return DRAM_NOC1_CONTROL_REG_ADDR_BASE_MAP[noc_port];
                }
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    }

    UMD_THROW(error::RuntimeError, fmt::format("Invalid NOC: {} for getting NOC register addr base.", noc));
}

static uint64_t noc_node_id_reg_addr(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    return noc_reg_base(core_type, noc, noc_port) + NOC_NODE_ID_OFFSET;
}

static uint64_t noc_translated_id_reg_addr(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    return noc_reg_base(core_type, noc, noc_port) + NOC_ID_TRANSLATED_OFFSET;
}

}  // namespace wormhole

namespace blackhole {

static uint64_t noc_reg_base(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    if (noc == 0) {
        for (const auto& noc_pair : NOC0_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    } else if (noc == 1) {
        for (const auto& noc_pair : NOC1_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    }

    UMD_THROW(error::RuntimeError, fmt::format("Invalid NOC: {} for getting NOC register addr base.", noc));
}

static uint64_t noc_node_id_reg_addr(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    return noc_reg_base(core_type, noc, noc_port) + NOC_NODE_ID_OFFSET;
}

static uint64_t noc_translated_id_reg_addr(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    return noc_reg_base(core_type, noc, noc_port) + NOC_ID_TRANSLATED_OFFSET;
}

}  // namespace blackhole

namespace grendel {

static uint64_t noc_reg_base(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    if (noc == 0) {
        for (const auto& noc_pair : NOC0_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    } else if (noc == 1) {
        for (const auto& noc_pair : NOC1_CONTROL_REG_ADDR_BASE_MAP) {
            if (noc_pair.first == core_type) {
                return noc_pair.second;
            }
        }
        UMD_THROW(error::RuntimeError, "Invalid core type for getting NOC register addr base.");
    }

    UMD_THROW(error::RuntimeError, fmt::format("Invalid NOC: {} for getting NOC register addr base.", noc));
}

static uint64_t noc_node_id_reg_addr(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    return noc_reg_base(core_type, noc, noc_port) + NOC_NODE_ID_OFFSET;
}

static uint64_t noc_translated_id_reg_addr(const CoreType core_type, const uint32_t noc, const uint32_t noc_port) {
    return noc_reg_base(core_type, noc, noc_port) + BH_NOC_ID_TRANSLATED_OFFSET;
}

}  // namespace grendel

ArchitectureRegisters get_architecture_registers(const tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return {
                .arc_apb_bar0_offset = wormhole::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = wormhole::ARC_CSM_BAR0_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = wormhole::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = wormhole::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = wormhole::ARC_APB_NOC_BASE_ADDRESS,
                .noc_node_id_bar_offset = wormhole::NIU_CFG_NOC0_BAR_ARC_ADDR + wormhole::NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = wormhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = &wormhole::noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = &wormhole::noc_translated_id_reg_addr,
            };
        case tt::ARCH::BLACKHOLE:
            return {
                .arc_apb_bar0_offset = blackhole::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = blackhole::ARC_CSM_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = blackhole::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = blackhole::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = blackhole::ARC_NOC_XBAR_ADDRESS_START,
                .noc_node_id_bar_offset = blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = blackhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = &blackhole::noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = &blackhole::noc_translated_id_reg_addr,
            };
        case tt::ARCH::QUASAR:
            return {
                .arc_apb_bar0_offset = grendel::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = grendel::ARC_CSM_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = grendel::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = grendel::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = grendel::ARC_NOC_XBAR_ADDRESS_START,
                .noc_node_id_bar_offset = grendel::BH_NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = grendel::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = &grendel::noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = &grendel::noc_translated_id_reg_addr,
            };
        default:
            UMD_THROW(error::RuntimeError, fmt::format("No registers defined for {} architecture.", arch_to_str(arch)));
    }
}

}  // namespace tt::umd
