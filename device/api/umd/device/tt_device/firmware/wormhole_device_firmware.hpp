// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device/firmware/device_firmware.hpp"

namespace tt::umd {

class ArchitectureImplementation;
class DeviceProtocol;
class JtagInterface;
class PcieInterface;
class RemoteInterface;

/**
 * @brief Wormhole implementation of DeviceFirmware.
 *
 * Built from protocol interfaces: the device protocol it issues NOC accesses through, exactly one of
 * the optional PCIe/JTAG/remote transports (which one is present is how routes are picked), and the
 * architecture implementation for register layout. All non-owning; they belong to the object that
 * owns this one and must outlive it.
 */
class WormholeDeviceFirmware : public DeviceFirmware {
public:
    WormholeDeviceFirmware(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        RemoteInterface* remote_interface,
        ArchitectureImplementation* architecture_impl);

private:
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    RemoteInterface* remote_interface_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
};

}  // namespace tt::umd
