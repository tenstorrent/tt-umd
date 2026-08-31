// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/blackhole_device_firmware.hpp"

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    architecture_impl_(architecture_impl) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "BlackholeDeviceFirmware requires a DeviceProtocol.");
    UMD_ASSERT(
        architecture_impl_ != nullptr,
        error::RuntimeError,
        "BlackholeDeviceFirmware requires an ArchitectureImplementation.");
    UMD_ASSERT(
        (pcie_interface_ != nullptr) != (jtag_interface_ != nullptr),
        error::RuntimeError,
        "BlackholeDeviceFirmware requires exactly one of a PcieInterface or a JtagInterface, since which one is "
        "present is how it picks the route for an access.");
}

}  // namespace tt::umd
