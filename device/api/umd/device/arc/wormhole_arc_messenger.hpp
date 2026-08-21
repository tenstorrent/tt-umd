// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/firmware/wormhole_arc_apb.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class WormholeArcMessenger : public ArcMessenger {
public:
    /**
     * Constructor for WormholeArcMessenger.
     *
     * @param device_protocol Protocol used for NOC accesses to the ARC core.
     * @param arc_core_noc0 ARC core coordinate on NOC0.
     * @param arc_core_noc1 ARC core coordinate on NOC1.
     * @param pcie_interface PCIe BAR access; null for a device not reached over PCIe.
     * @param jtag_interface JTAG access; null for a device not reached over JTAG.
     * @param remote_interface Ethernet gateway access; null for a local device.
     */
    WormholeArcMessenger(
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
     * @param args Arguments for the message. For Wormhole, only 2 args are allowed, each <= uint16_t max.
     * @param timeout_ms Timeout in milliseconds; 0 to wait indefinitely.
     */
    uint32_t send_message(
        const uint32_t msg_code,
        std::vector<uint32_t>& return_values,
        const std::vector<uint32_t>& args = {},
        const std::chrono::milliseconds timeout_ms = timeout::ARC_MESSAGE_TIMEOUT) override;

private:
    // Thin wrappers that resolve the ARC core for noc_id and hand the access to arc_apb_.
    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    // Non-owning; belongs to the component that owns this messenger and must outlive it.
    RemoteInterface* remote_interface_ = nullptr;

    // The ARC APB window needs an architecture_implementation, and the architecture is fixed for this
    // class, so it owns one rather than taking it from the caller. Declared before arc_apb_ so it is
    // constructed first.
    std::unique_ptr<architecture_implementation> architecture_impl_;

    WormholeArcApb arc_apb_;
};

}  // namespace tt::umd
