// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arc/blackhole_arc_message_queue.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class BlackholeArcMessenger : public ArcMessenger {
public:
    /**
     * Constructor for BlackholeArcMessenger.
     *
     * @param device_protocol Protocol used for NOC accesses to the ARC core.
     * @param arc_core_noc0 ARC core coordinate on NOC0.
     * @param arc_core_noc1 ARC core coordinate on NOC1.
     * @param pcie_interface PCIe BAR access; null for a device not reached over PCIe.
     * @param jtag_interface JTAG access; null for a device not reached over JTAG.
     */
    BlackholeArcMessenger(
        DeviceProtocol* device_protocol,
        xy_pair arc_core_noc0,
        xy_pair arc_core_noc1,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface = nullptr);

    /**
     * Send ARC message. The call of send_message is blocking, timeout is to be implemented.
     *
     * @param msg_code ARC messsage type.
     * @param return_values Return values from the ARC message.
     * @param args Arguments for the message. For Blackhole, up to 7 args are allowed.
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT) override;

private:
    // The ARC APB window needs an architecture_implementation, and the architecture is fixed for this
    // class, so it owns one rather than taking it from the caller. Declared before arc_apb so it is
    // constructed first.
    std::unique_ptr<architecture_implementation> architecture_impl_;

    // Declared before the queue so it outlives it: the queue holds a raw pointer to this.
    BlackholeArcApb arc_apb;
    std::unique_ptr<BlackholeArcMessageQueue> blackhole_arc_msg_queue = nullptr;
};

}  // namespace tt::umd
