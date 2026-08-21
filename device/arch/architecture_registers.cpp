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
                .noc_node_id_bar_offset = wormhole::NIU_CFG_NOC0_BAR_ARC_ADDR + wormhole::NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = wormhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = &wormhole::noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = &wormhole::noc_translated_id_reg_addr,
            };
        case tt::ARCH::BLACKHOLE:
            return {
                .noc_node_id_bar_offset = blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = blackhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = &blackhole::noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = &blackhole::noc_translated_id_reg_addr,
            };
        case tt::ARCH::QUASAR:
            return {
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
