// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

// A read returning all ones is how a hung bus or NOC shows up on every architecture.
inline constexpr uint32_t HANG_READ_VALUE = 0xFFFFFFFFu;

// Resolves the address of a NOC register within the control register block of a core type.
using NocRegAddrResolver = uint64_t (*)(CoreType core_type, uint32_t noc, uint32_t noc_port);

// Addresses of device registers which mean the same thing on every architecture but sit at
// different addresses. Architecture specific code reads its own constants directly; this exists for
// callers which are not tied to one architecture.
struct ArchitectureRegisters {
    // ARC register windows. BAR0 offsets are usable before the device is initialized, the NOC base
    // address only once the NOC is up.
    uint32_t arc_apb_bar0_offset;
    uint32_t arc_csm_bar0_mailbox_offset;
    // ARC reset unit scratch registers, relative to the APB window.
    uint32_t arc_reset_scratch_offset;
    uint32_t arc_reset_scratch_2_offset;
    uint64_t arc_apb_noc_base_address;

    // BAR0 offset of the NOC0 node id register. Reachable before the NOC is up, which is what makes
    // it usable as the bus hang check.
    uint32_t noc_node_id_bar_offset;

    // Tensix RISCV debug bus control register.
    uint32_t riscv_debug_bus_cntl_reg;

    // The NOC node id registers sit in a per core type control register block, so their address is a
    // lookup rather than a fixed offset.
    NocRegAddrResolver get_noc_node_id_reg_addr;
    NocRegAddrResolver get_noc_translated_id_reg_addr;
};

ArchitectureRegisters get_architecture_registers(const tt::ARCH arch);

}  // namespace tt::umd
