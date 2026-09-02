// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/firmware/wormhole_arc_window.hpp"

#include <fmt/format.h>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// The constructor enforces that exactly one transport interface is given: which one is present
// is how the accesses added by later PRs pick their route.

/* static */ WormholeArcWindow WormholeArcWindow::arc_apb(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    static constexpr Config config{
        .name = "ARC APB",
        .noc_base_address = wormhole::ARC_APB_NOC_BASE_ADDRESS,
        .bar0_offset_start = wormhole::ARC_APB_BAR0_XBAR_OFFSET_START,
        // ARC_APB_ADDRESS_RANGE is END - START with an inclusive END, so it is the last valid
        // offset rather than the window size. The bound below is a size, hence the + 1.
        .size_bytes = wormhole::ARC_APB_ADDRESS_RANGE + 1,
        .content = Content::REGISTERS,
    };
    return WormholeArcWindow(config, device_protocol, pcie_interface, jtag_interface, remote_interface);
}

/* static */ WormholeArcWindow WormholeArcWindow::arc_csm(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    static constexpr Config config{
        .name = "ARC CSM",
        .noc_base_address = wormhole::ARC_CSM_NOC_BASE_ADDRESS,
        .bar0_offset_start = wormhole::ARC_CSM_BAR0_XBAR_OFFSET_START,
        .size_bytes = wormhole::ARC_CSM_ADDRESS_RANGE + 1,
        .content = Content::MEMORY,
    };
    return WormholeArcWindow(config, device_protocol, pcie_interface, jtag_interface, remote_interface);
}

WormholeArcWindow::WormholeArcWindow(
    const Config& config,
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) :
    config_(config),
    device_protocol_(device_protocol),
    pcie_interface_(pcie_interface),
    jtag_interface_(jtag_interface),
    remote_interface_(remote_interface) {
    UMD_ASSERT(
        device_protocol_ != nullptr,
        error::RuntimeError,
        fmt::format("The {} window requires a DeviceProtocol.", config_.name));
    const int transports = (pcie_interface_ != nullptr) + (jtag_interface_ != nullptr) + (remote_interface_ != nullptr);
    UMD_ASSERT(
        transports == 1,
        error::RuntimeError,
        fmt::format(
            "The {} window requires exactly one of a PcieInterface, a JtagInterface or a RemoteInterface, since "
            "which one is present is how it picks the route for an access.",
            config_.name));
}

}  // namespace tt::umd
