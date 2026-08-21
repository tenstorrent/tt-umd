// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class RemoteInterface;

class ArcMessenger {
public:
    /**
     * Create an ArcMessenger object.
     *
     * The communication protocol is inferred from which of the optional interfaces is non-null: JTAG
     * when jtag_interface is set, ethernet through a gateway when remote_interface is set, PCIe
     * otherwise. This holds because a device is reached over exactly one protocol.
     *
     * @param arch Architecture of the device, selecting the ArcMessenger implementation.
     * @param device_protocol Protocol used for NOC accesses to the ARC core.
     * @param arc_core_noc0 ARC core coordinate on NOC0.
     * @param arc_core_noc1 ARC core coordinate on NOC1.
     * @param pcie_interface PCIe BAR access; null for a device not reached over PCIe.
     * @param jtag_interface JTAG access; null for a device not reached over JTAG.
     * @param remote_interface Ethernet gateway access; null for a local device. Wormhole only.
     * @return Unique pointer to ArcMessenger object.
     */
    static std::unique_ptr<ArcMessenger> create_arc_messenger(
        tt::ARCH arch,
        DeviceProtocol* device_protocol,
        xy_pair arc_core_noc0,
        xy_pair arc_core_noc1,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface = nullptr,
        RemoteInterface* remote_interface = nullptr);

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     *
     * @param msg_code ARC messsage type.
     * @param return_values Return values from the ARC message.
     * @param args Arguments for the message (device-specific limits apply).
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     * @return Success code of the ARC message.
     */
    virtual uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT) = 0;

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     * This version of the function can be called if the return values are not needed.
     *
     * @param msg_code ARC messsage type.
     * @param args Arguments for the message (device-specific limits apply).
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     * @return Success code of the ARC message.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT);

    virtual ~ArcMessenger();

protected:
    ArcMessenger(
        DeviceProtocol* device_protocol,
        xy_pair arc_core_noc0,
        xy_pair arc_core_noc1,
        IODeviceType io_device_type = IODeviceType::PCIe);

    // Returns the ARC core coordinate for the NOC the caller is routing over.
    xy_pair get_arc_core(NocId noc_id) const;

    // Non-owning; belongs to the component that owns this messenger and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    xy_pair arc_core_noc0_;
    xy_pair arc_core_noc1_;
    IODeviceType io_device_type_ = IODeviceType::PCIe;
    // Names the ARC message mutex; taken from the protocol so it identifies this device, not the
    // silicon model. See DeviceProtocol::get_mmio_id().
    int mmio_id = 0;

    LockManager lock_manager;
};

}  // namespace tt::umd
