// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_registers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "common/unsupported.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

template <std::size_t N>
uint64_t control_reg_base(const std::array<std::pair<CoreType, uint64_t>, N>& base_map, CoreType core_type) {
    for (const auto& [type, base] : base_map) {
        if (type == core_type) {
            return base;
        }
    }
    UMD_THROW(error::RuntimeError, "Core type " + to_str(core_type) + " has no NOC control registers.");
}

// Blackhole has one control register block per core type and NOC, whatever the NOC port.
uint64_t blackhole_noc_reg_base(CoreType core_type, uint32_t noc) {
    return noc == 0 ? control_reg_base(blackhole::NOC0_CONTROL_REG_ADDR_BASE_MAP, core_type)
                    : control_reg_base(blackhole::NOC1_CONTROL_REG_ADDR_BASE_MAP, core_type);
}

uint64_t blackhole_noc_node_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t /*noc_port*/) {
    return blackhole_noc_reg_base(core_type, noc) + blackhole::NOC_NODE_ID_OFFSET;
}

uint64_t blackhole_noc_translated_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t /*noc_port*/) {
    return blackhole_noc_reg_base(core_type, noc) + blackhole::NOC_ID_TRANSLATED_OFFSET;
}

// Each of the three NOC ports of a Wormhole DRAM tile has its own control register block.
uint64_t wormhole_noc_reg_base(CoreType core_type, uint32_t noc, uint32_t noc_port) {
    if (core_type == CoreType::DRAM) {
        return noc == 0 ? wormhole::DRAM_NOC0_CONTROL_REG_ADDR_BASE_MAP.at(noc_port)
                        : wormhole::DRAM_NOC1_CONTROL_REG_ADDR_BASE_MAP.at(noc_port);
    }
    return noc == 0 ? control_reg_base(wormhole::NOC0_CONTROL_REG_ADDR_BASE_MAP, core_type)
                    : control_reg_base(wormhole::NOC1_CONTROL_REG_ADDR_BASE_MAP, core_type);
}

uint64_t wormhole_noc_node_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t noc_port) {
    return wormhole_noc_reg_base(core_type, noc, noc_port) + wormhole::NOC_NODE_ID_OFFSET;
}

uint64_t wormhole_noc_translated_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t noc_port) {
    return wormhole_noc_reg_base(core_type, noc, noc_port) + wormhole::NOC_ID_TRANSLATED_OFFSET;
}

}  // namespace

ArchitectureRegisters get_architecture_registers(const tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::BLACKHOLE:
            return {
                .arc_apb_bar0_offset = blackhole::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = blackhole::ARC_CSM_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = blackhole::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = blackhole::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = blackhole::ARC_NOC_XBAR_ADDRESS_START,
                // The PCIe tile's own NIU, so the read never leaves the PCIe tile.
                .noc_node_id_bar_offset = blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = blackhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = blackhole_noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = blackhole_noc_translated_id_reg_addr,
            };
        case tt::ARCH::WORMHOLE_B0:
            return {
                .arc_apb_bar0_offset = wormhole::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = wormhole::ARC_CSM_BAR0_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = wormhole::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = wormhole::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = wormhole::ARC_APB_NOC_BASE_ADDRESS,
                .noc_node_id_bar_offset = wormhole::NIU_CFG_NOC0_BAR_ARC_ADDR + wormhole::NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = wormhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = wormhole_noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = wormhole_noc_translated_id_reg_addr,
            };
        default:
            UMD_NOT_SUPPORTED("Register map of " + arch_to_str(arch));
    }
}

}  // namespace tt::umd
