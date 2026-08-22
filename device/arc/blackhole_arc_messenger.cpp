// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_messenger.hpp"

#include <fmt/ranges.h>

#include <chrono>
#include <tt-logger/tt-logger.hpp>

#include "noc_access.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

BlackholeArcMessenger::BlackholeArcMessenger(TTDevice* tt_device) : ArcMessenger(tt_device) {
    // get_pcie_interface()/get_jtag_interface() throw when the device was not opened that way, so
    // only the one matching this device's protocol is fetched; the other stays null.
    const bool is_jtag = tt_device->get_communication_device_type() == IODeviceType::JTAG;
    JtagInterface* jtag_interface = is_jtag ? tt_device->get_jtag_interface() : nullptr;
    PcieInterface* pcie_interface = is_jtag ? nullptr : tt_device->get_pcie_interface();

    arc_apb = std::make_unique<BlackholeArcApb>(
        tt_device->get_device_protocol(), pcie_interface, jtag_interface, tt_device->get_architecture_implementation());

    blackhole_arc_msg_queue = BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
        tt_device->get_device_protocol(),
        jtag_interface,
        arc_apb.get(),
        tt_device->get_noc_translation_enabled(),
        BlackholeArcMessageQueueIndex::APPLICATION,
        get_selected_noc_id());
}

uint32_t BlackholeArcMessenger::send_message(
    const uint32_t msg_code,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms) {
    auto lock = lock_manager.acquire_mutex(MutexType::ARC_MSG, tt_device->get_pci_device()->get_device_num());
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
