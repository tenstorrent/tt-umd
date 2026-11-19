// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/firmware/grendel_device_firmware.hpp"

#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/communication_protocol.hpp"
#include "tt-umd/utils/lock_manager.hpp"

namespace tt::umd {

namespace {

// The SMC on the Keraunos I/O chiplet, the one chiplet every Quasar package topology has.
constexpr uint64_t KERAUNOS_SMC_BASE = 0x1202000000;

}  // namespace

GrendelDeviceFirmware::GrendelDeviceFirmware(DeviceProtocol* device_protocol, bool kmd_lock_available) :
    message_queue_(device_protocol, KERAUNOS_SMC_BASE) {
    // Read only after the queue has rejected a null protocol.
    device_id_ = device_protocol->get_mmio_id();

    // acquire_mutex() throws unless the mutex was initialized first, so claim it up front.
    LockManager::initialize_mutex(MutexType::ARC_MSG, device_id_, IODeviceType::PCIe, kmd_lock_available);
}

DeviceCommandResult GrendelDeviceFirmware::send_device_command(
    uint32_t msg_code,
    const std::vector<uint32_t>& args,
    std::chrono::milliseconds timeout,
    [[maybe_unused]] NocId noc_id) {
    // The device does not arbitrate between host users of the queue, so this lock is all that keeps
    // two senders from overwriting each other's slots.
    auto lock = LockManager::acquire_mutex(MutexType::ARC_MSG, device_id_, IODeviceType::PCIe);

    const auto response = message_queue_.send_message(msg_code, args, timeout);
    return DeviceCommandResult{response[0], std::vector<uint32_t>(response.begin() + 1, response.end())};
}

}  // namespace tt::umd
