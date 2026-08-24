// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// How this class picks a route: a non-null JtagInterface means the device is reached over JTAG,
// otherwise it is reached over PCIe. Inferring the protocol from which optional interface is present
// is sound because a TTDevice is built for exactly one communication protocol - reaching the same
// chip over both PCIe and JTAG today requires two TTDevice objects, so the two interfaces are never
// both handed to the same object. If UMD ever supports using both protocols against a single device,
// this has to become an explicit protocol selection rather than a null check.

BlackholeArcApb::BlackholeArcApb(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    architecture_impl_(architecture_impl) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "BlackholeArcApb requires a DeviceProtocol.");
    UMD_ASSERT(
        architecture_impl_ != nullptr,
        error::RuntimeError,
        "BlackholeArcApb requires an ArchitectureImplementation.");
}

void BlackholeArcApb::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (arc_addr_offset > blackhole::ARC_XBAR_ADDRESS_END) {
        UMD_THROW(error::RuntimeError, "Address is out of ARC XBAR address range.");
    }

    if (jtag_interface_ != nullptr) {
        device_protocol_->read_ctrl(
            mem_ptr, arc_core, blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset, sizeof(uint32_t), noc_id);
        return;
    }
    if (!is_arc_available_over_axi()) {
        device_protocol_->read_ctrl(
            mem_ptr, arc_core, architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size, noc_id);
        return;
    }
    auto result = pcie_interface_->bar_read32(blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t*>(mem_ptr)) = result;
}

void BlackholeArcApb::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (arc_addr_offset > blackhole::ARC_XBAR_ADDRESS_END) {
        UMD_THROW(error::RuntimeError, "Address is out of ARC XBAR address range.");
    }

    if (jtag_interface_ != nullptr) {
        device_protocol_->write_ctrl(
            mem_ptr, arc_core, blackhole::ARC_NOC_XBAR_ADDRESS_START + arc_addr_offset, sizeof(uint32_t), noc_id);
        return;
    }
    if (!is_arc_available_over_axi()) {
        device_protocol_->write_ctrl(
            mem_ptr, arc_core, architecture_impl_->get_arc_apb_noc_base_address() + arc_addr_offset, size, noc_id);
        return;
    }
    pcie_interface_->bar_write32(
        blackhole::ARC_APB_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t*>(mem_ptr)));
}

// ARC tile accessibility over AXI via PCIe depends on the PCIe tile's x-coordinate:
// x = 2: ARC not accessible, x = 11: ARC accessible
bool BlackholeArcApb::is_arc_available_over_axi() { return (get_pcie_x_coordinate() == 11); }

int BlackholeArcApb::get_pcie_x_coordinate() {
    // Extract the x-coordinate from the register using the lower 6 bits.
    return pcie_interface_->bar_read32(architecture_impl_->get_read_checking_offset()) & 0x3F;
}

}  // namespace tt::umd
