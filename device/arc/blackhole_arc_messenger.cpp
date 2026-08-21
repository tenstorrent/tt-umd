// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_messenger.hpp"

#include <fmt/ranges.h>

#include <chrono>
#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

// How this class picks a route for ARC accesses: a non-null JtagInterface means the device is reached
// over JTAG, otherwise it is reached over PCIe. Inferring the route from which optional interface is
// present is sound because a device is reached over exactly one communication protocol. The routing
// itself lives in BlackholeArcApb.

BlackholeArcMessenger::BlackholeArcMessenger(
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface) :
    ArcMessenger(
        device_protocol,
        arc_core_noc0,
        arc_core_noc1,
        jtag_interface != nullptr ? IODeviceType::JTAG : IODeviceType::PCIe),
    architecture_impl_(architecture_implementation::create(tt::ARCH::BLACKHOLE)),
    arc_apb(device_protocol, pcie_interface, jtag_interface, architecture_impl_.get()) {
    blackhole_arc_msg_queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        device_protocol,
        jtag_interface,
        &arc_apb,
        arc_core_noc0_,
        arc_core_noc1_,
        BlackholeArcMessageQueueIndex::APPLICATION,
        get_selected_noc_id());
}

uint32_t BlackholeArcMessenger::send_message(
    const uint32_t msg_code,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms) {
    auto lock = lock_manager.acquire_mutex(MutexType::ARC_MSG, mmio_id, io_device_type_);
    uint32_t exit_code = blackhole_arc_msg_queue->send_message(
        (ArcMessageType)msg_code, return_values, args, timeout_ms, get_selected_noc_id());
    log_debug(
        LogUMD,
        "ARC message 0x{:x} returned exit_code={} return_values=[{}]",
        msg_code,
        exit_code,
        fmt::join(return_values, ", "));
    return exit_code;
}

}  // namespace tt::umd
