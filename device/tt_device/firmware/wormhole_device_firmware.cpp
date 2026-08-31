// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

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
    architecture_impl_(architecture_impl) {
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
}

}  // namespace tt::umd
