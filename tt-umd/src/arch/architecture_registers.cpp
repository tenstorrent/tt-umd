// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arch/architecture_registers.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <utility>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/grendel_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// BAR0 offset of the PCIe tile's NOC0 NIU on Wormhole; the node id register sits at NOC_NODE_ID_OFFSET in it.
constexpr uint32_t WORMHOLE_PCIE_NIU_NOC0_BAR0_OFFSET = 0x1FD20000;

template <size_t N>
uint64_t lookup_control_reg_base(const std::array<std::pair<CoreType, uint64_t>, N>& base_map, CoreType core_type) {
    for (const auto& [type, base] : base_map) {
        if (type == core_type) {
            return base;
        }
    }
    UMD_THROW(
        error::RuntimeError, fmt::format("Core type {} has no NOC control register block.", tt::to_str(core_type)));
}

uint64_t wormhole_control_reg_base(CoreType core_type, uint32_t noc, uint32_t noc_port) {
    // Each port of a Wormhole DRAM bank has its own NIU, so the register block depends on the port.
    if (core_type == CoreType::DRAM) {
        const auto& port_bases =
            noc == 0 ? wormhole::DRAM_NOC0_CONTROL_REG_ADDR_BASE_MAP : wormhole::DRAM_NOC1_CONTROL_REG_ADDR_BASE_MAP;
        return port_bases.at(noc_port);
    }
    return lookup_control_reg_base(
        noc == 0 ? wormhole::NOC0_CONTROL_REG_ADDR_BASE_MAP : wormhole::NOC1_CONTROL_REG_ADDR_BASE_MAP, core_type);
}

uint64_t wormhole_noc_node_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t noc_port) {
    return wormhole_control_reg_base(core_type, noc, noc_port) + wormhole::NOC_NODE_ID_OFFSET;
}

uint64_t wormhole_noc_translated_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t noc_port) {
    return wormhole_control_reg_base(core_type, noc, noc_port) + wormhole::NOC_ID_TRANSLATED_OFFSET;
}

uint64_t blackhole_control_reg_base(CoreType core_type, uint32_t noc) {
    return lookup_control_reg_base(
        noc == 0 ? blackhole::NOC0_CONTROL_REG_ADDR_BASE_MAP : blackhole::NOC1_CONTROL_REG_ADDR_BASE_MAP, core_type);
}

uint64_t blackhole_noc_node_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t /*noc_port*/) {
    return blackhole_control_reg_base(core_type, noc) + blackhole::NOC_NODE_ID_OFFSET;
}

uint64_t blackhole_noc_translated_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t /*noc_port*/) {
    return blackhole_control_reg_base(core_type, noc) + blackhole::NOC_ID_TRANSLATED_OFFSET;
}

uint64_t grendel_control_reg_base(CoreType core_type, uint32_t noc) {
    return lookup_control_reg_base(
        noc == 0 ? grendel::NOC0_CONTROL_REG_ADDR_BASE_MAP : grendel::NOC1_CONTROL_REG_ADDR_BASE_MAP, core_type);
}

uint64_t grendel_noc_node_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t /*noc_port*/) {
    return grendel_control_reg_base(core_type, noc) + grendel::NOC_NODE_ID_OFFSET;
}

uint64_t grendel_noc_translated_id_reg_addr(CoreType core_type, uint32_t noc, uint32_t /*noc_port*/) {
    return grendel_control_reg_base(core_type, noc) + grendel::BH_NOC_ID_TRANSLATED_OFFSET;
}

}  // namespace

ArchitectureRegisters get_architecture_registers(const tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return ArchitectureRegisters{
                .arc_apb_bar0_offset = wormhole::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = wormhole::ARC_CSM_BAR0_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = wormhole::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = wormhole::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = wormhole::ARC_APB_NOC_BASE_ADDRESS,
                .noc_node_id_bar_offset =
                    WORMHOLE_PCIE_NIU_NOC0_BAR0_OFFSET + static_cast<uint32_t>(wormhole::NOC_NODE_ID_OFFSET),
                .riscv_debug_bus_cntl_reg = wormhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = wormhole_noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = wormhole_noc_translated_id_reg_addr,
            };
        case tt::ARCH::BLACKHOLE:
            return ArchitectureRegisters{
                .arc_apb_bar0_offset = blackhole::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = blackhole::ARC_CSM_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = blackhole::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = blackhole::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = blackhole::ARC_NOC_XBAR_ADDRESS_START,
                .noc_node_id_bar_offset =
                    blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + static_cast<uint32_t>(blackhole::NOC_NODE_ID_OFFSET),
                .riscv_debug_bus_cntl_reg = blackhole::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = blackhole_noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = blackhole_noc_translated_id_reg_addr,
            };
        case tt::ARCH::QUASAR:
            return ArchitectureRegisters{
                .arc_apb_bar0_offset = grendel::ARC_APB_BAR0_XBAR_OFFSET_START,
                .arc_csm_bar0_mailbox_offset = grendel::ARC_CSM_MAILBOX_OFFSET,
                .arc_reset_scratch_offset = grendel::ARC_RESET_SCRATCH_OFFSET,
                .arc_reset_scratch_2_offset = grendel::ARC_RESET_SCRATCH_2_OFFSET,
                .arc_apb_noc_base_address = grendel::ARC_NOC_XBAR_ADDRESS_START,
                .noc_node_id_bar_offset = grendel::BH_NOC_NODE_ID_OFFSET,
                .riscv_debug_bus_cntl_reg = grendel::RISCV_DEBUG_REG_DBG_BUS_CNTL_REG,
                .get_noc_node_id_reg_addr = grendel_noc_node_id_reg_addr,
                .get_noc_translated_id_reg_addr = grendel_noc_translated_id_reg_addr,
            };
        default:
            UMD_THROW(
                error::RuntimeError, fmt::format("No register map exists for architecture {}.", arch_to_str(arch)));
    }
}

}  // namespace tt::umd
