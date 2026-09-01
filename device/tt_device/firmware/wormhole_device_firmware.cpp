// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// How this class picks a route for ARC accesses: a non-null RemoteInterface means the device is
// reached over ethernet through a gateway, a non-null JtagInterface means it is reached over JTAG,
// and otherwise it is reached over PCIe. Inferring the route from which optional interface is
// present is sound because a TTDevice is built for exactly one communication protocol. The routing
// itself lives in WormholeArcWindow.

WormholeDeviceFirmware::WormholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    remote_interface_(remote_interface),
    architecture_impl_(architecture_impl),
    arc_apb_(WormholeArcWindow::arc_apb(device_protocol, pcie_interface, jtag_interface, remote_interface)),
    arc_csm_(WormholeArcWindow::arc_csm(device_protocol, pcie_interface, jtag_interface, remote_interface)) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "WormholeDeviceFirmware requires a DeviceProtocol.");
    UMD_ASSERT(
        architecture_impl_ != nullptr,
        error::RuntimeError,
        "WormholeDeviceFirmware requires an ArchitectureImplementation.");
    const int transports = (pcie_interface_ != nullptr) + (jtag_interface_ != nullptr) + (remote_interface_ != nullptr);
    UMD_ASSERT(
        transports == 1,
        error::RuntimeError,
        "WormholeDeviceFirmware requires exactly one of a PcieInterface, a JtagInterface or a RemoteInterface, since "
        "which one is present is how it picks the route for an access.");

    // Read after the checks above, not in the member initialiser list: that runs first, so a null
    // protocol faulted there before the assert could report it.
    device_id_ = device_protocol_->get_mmio_id();

    // The ARC core is at a fixed NOC0 coordinate on Wormhole, so both coordinates are known without
    // reading anything from the device.
    arc_core_noc0_ = wormhole::ARC_CORES_NOC0[0];
    arc_core_noc1_ = tt_xy_pair(
        wormhole::NOC0_X_TO_NOC1_X[wormhole::ARC_CORES_NOC0[0].x],
        wormhole::NOC0_Y_TO_NOC1_Y[wormhole::ARC_CORES_NOC0[0].y]);
}

IODeviceType WormholeDeviceFirmware::get_io_device_type() const {
    return jtag_interface_ != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe;
}

tt_xy_pair WormholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1_ : arc_core_noc0_;
}

void WormholeDeviceFirmware::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_apb_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

void WormholeDeviceFirmware::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id) {
    arc_csm_.read(mem_ptr, arc_addr_offset, size, get_firmware_noc_coord(noc_id), noc_id);
}

}  // namespace tt::umd
