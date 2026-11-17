// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"

#include <cstddef>
#include <cstdint>

#include "common/unsupported.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/tt_device/protocol/pcie_interface.hpp"
#include "tt-umd/types/register_access_width.hpp"

namespace tt::umd {

namespace {

// NOC0 x coordinate of the PCIe tile whose AXI port reaches the ARC APB window through BAR0. Through the other
// PCIe tile (x = 2, as on a P150) every BAR0 read of the window returns all ones.
constexpr int ARC_AXI_PCIE_X = 11;

}  // namespace

BlackholeArcApb::BlackholeArcApb(
    DeviceProtocol* device_protocol, PcieInterface* pcie_interface, JtagInterface* jtag_interface) :
    device_protocol_(device_protocol), pcie_interface_(pcie_interface), jtag_interface_(jtag_interface) {
    UMD_ASSERT(
        (pcie_interface_ == nullptr) != (jtag_interface_ == nullptr),
        error::RuntimeError,
        "ARC APB access needs exactly one of a PCIe and a JTAG interface.");
}

void BlackholeArcApb::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (jtag_interface_ != nullptr) {
        UMD_NOT_SUPPORTED("ARC APB access over JTAG");
    }
    if (!is_arc_available_over_axi()) {
        device_protocol_->read_ctrl(
            mem_ptr,
            arc_core,
            blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset,
            size,
            noc_id,
            RegisterAccessWidth::BITS32);
        return;
    }
    *static_cast<uint32_t*>(mem_ptr) =
        pcie_interface_->bar_read32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset);
}

void BlackholeArcApb::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (jtag_interface_ != nullptr) {
        UMD_NOT_SUPPORTED("ARC APB access over JTAG");
    }
    if (!is_arc_available_over_axi()) {
        device_protocol_->write_ctrl(
            mem_ptr,
            arc_core,
            blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset,
            size,
            noc_id,
            RegisterAccessWidth::BITS32);
        return;
    }
    pcie_interface_->bar_write32(
        blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset, *static_cast<const uint32_t*>(mem_ptr));
}

bool BlackholeArcApb::is_arc_available_over_axi() { return get_pcie_x_coordinate() == ARC_AXI_PCIE_X; }

int BlackholeArcApb::get_pcie_x_coordinate() {
    // The PCIe tile's own NIU answers over BAR0 without leaving the tile, so this works even with a hung NOC.
    const uint32_t node_id =
        pcie_interface_->bar_read32(blackhole::NIU_CFG_NOC0_BAR_PCIE_ADDR + blackhole::NOC_NODE_ID_OFFSET);
    return static_cast<int>(node_id & 0x3F);
}

}  // namespace tt::umd
