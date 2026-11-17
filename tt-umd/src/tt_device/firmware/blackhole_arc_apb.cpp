// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// NOC0 X of the PCIe tile that has the ARC window in its BAR0. The other PCIe tile reserves that range
// and reaches the ARC only over the NOC.
constexpr uint32_t ARC_ON_AXI_PCIE_X = 11;

constexpr uint32_t NOC_NODE_ID_X_MASK = 0x3F;

}  // namespace

BlackholeArcApb::BlackholeArcApb(
    DeviceProtocol* device_protocol, PcieInterface* pcie_interface, JtagInterface* jtag_interface) :
    device_protocol_(device_protocol), pcie_interface_(pcie_interface), jtag_interface_(jtag_interface) {}

void BlackholeArcApb::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (jtag_interface_ != nullptr) {
        UMD_THROW(error::RuntimeError, "ARC APB access over JTAG is not supported.");
    }
    if (is_arc_available_over_axi()) {
        *static_cast<uint32_t*>(mem_ptr) =
            pcie_interface_->bar_read32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset);
        return;
    }
    device_protocol_->read_ctrl(
        mem_ptr,
        arc_core,
        blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset,
        size,
        noc_id,
        RegisterAccessWidth::BITS32);
}

void BlackholeArcApb::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (jtag_interface_ != nullptr) {
        UMD_THROW(error::RuntimeError, "ARC APB access over JTAG is not supported.");
    }
    if (is_arc_available_over_axi()) {
        pcie_interface_->bar_write32(
            blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset, *static_cast<const uint32_t*>(mem_ptr));
        return;
    }
    device_protocol_->write_ctrl(
        mem_ptr,
        arc_core,
        blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset,
        size,
        noc_id,
        RegisterAccessWidth::BITS32);
}

bool BlackholeArcApb::is_arc_available_over_axi() {
    return pcie_interface_ != nullptr && get_pcie_x_coordinate() == ARC_ON_AXI_PCIE_X;
}

int BlackholeArcApb::get_pcie_x_coordinate() {
    // The host-facing PCIe tile's own NOC0 node id, read through its BAR0 NIU block.
    const uint32_t node_id =
        pcie_interface_->bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET);
    return static_cast<int>(node_id & NOC_NODE_ID_X_MASK);
}

}  // namespace tt::umd
