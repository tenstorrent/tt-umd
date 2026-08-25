// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_arc_csm.hpp"

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// Route selection is the same null-check scheme WormholeArcApb documents, and the constructor
// enforces the same exactly-one-transport invariant it relies on.

WormholeArcCsm::WormholeArcCsm(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    remote_interface_(remote_interface) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "WormholeArcCsm requires a DeviceProtocol.");
    const int transports = (pcie_interface_ != nullptr) + (jtag_interface_ != nullptr) + (remote_interface_ != nullptr);
    UMD_ASSERT(
        transports == 1,
        error::RuntimeError,
        "WormholeArcCsm requires exactly one of a PcieInterface, a JtagInterface or a RemoteInterface, since which "
        "one is present is how it picks the route for an access.");
}

void WormholeArcCsm::read(void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        UMD_THROW(error::RuntimeError, "Address is out of ARC CSM address range.");
    }

    if (remote_interface_ != nullptr) {
        // CSM is memory rather than registers, so the remote path is a data access. Mirrors
        // WormholeTTDevice::read_from_arc_csm, which uses read_from_device here and
        // read_from_device_reg on the APB path.
        device_protocol_->read_data(
            mem_ptr, arc_core, wormhole::ARC_CSM_NOC_BASE_ADDRESS + arc_addr_offset, size, noc_id);
        return;
    }
    if (jtag_interface_ != nullptr) {
        device_protocol_->read_ctrl(
            mem_ptr, arc_core, wormhole::ARC_CSM_NOC_BASE_ADDRESS + arc_addr_offset, sizeof(uint32_t), noc_id);
        return;
    }
    auto result = pcie_interface_->bar_read32(wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + arc_addr_offset);
    *(reinterpret_cast<uint32_t*>(mem_ptr)) = result;
}

void WormholeArcCsm::write(
    const void* mem_ptr, uint64_t arc_addr_offset, size_t size, tt_xy_pair arc_core, NocId noc_id) {
    if (arc_addr_offset > wormhole::ARC_CSM_ADDRESS_RANGE) {
        UMD_THROW(error::RuntimeError, "Address is out of ARC CSM address range.");
    }

    if (remote_interface_ != nullptr) {
        device_protocol_->write_data(
            mem_ptr, arc_core, wormhole::ARC_CSM_NOC_BASE_ADDRESS + arc_addr_offset, size, noc_id);
        return;
    }
    if (jtag_interface_ != nullptr) {
        device_protocol_->write_ctrl(
            mem_ptr, arc_core, wormhole::ARC_CSM_NOC_BASE_ADDRESS + arc_addr_offset, sizeof(uint32_t), noc_id);
        return;
    }
    pcie_interface_->bar_write32(
        wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START + arc_addr_offset, *(reinterpret_cast<const uint32_t*>(mem_ptr)));
}

}  // namespace tt::umd
