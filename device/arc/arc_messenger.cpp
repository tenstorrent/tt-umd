// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/arc_messenger.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/arc/blackhole_arc_messenger.hpp"
#include "umd/device/arc/wormhole_arc_messenger.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<ArcMessenger> ArcMessenger::create_arc_messenger(
    tt::ARCH arch,
    DeviceProtocol* device_protocol,
    xy_pair arc_core_noc0,
    xy_pair arc_core_noc1,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    RemoteInterface* remote_interface) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WormholeArcMessenger>(
                device_protocol, arc_core_noc0, arc_core_noc1, pcie_interface, jtag_interface, remote_interface);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<BlackholeArcMessenger>(
                device_protocol, arc_core_noc0, arc_core_noc1, pcie_interface, jtag_interface);
        default:
            UMD_THROW(error::RuntimeError, "Unsupported architecture for creating ArcMessenger.");
    }
}

ArcMessenger::ArcMessenger(
    DeviceProtocol* device_protocol, xy_pair arc_core_noc0, xy_pair arc_core_noc1, IODeviceType io_device_type) :
    device_protocol_(device_protocol),
    arc_core_noc0_(arc_core_noc0),
    arc_core_noc1_(arc_core_noc1),
    io_device_type_(io_device_type),
    mmio_id(device_protocol->get_mmio_id()) {
    lock_manager.initialize_mutex(MutexType::ARC_MSG, mmio_id, io_device_type_);
    lock_manager.initialize_mutex(MutexType::REMOTE_ARC_MSG, mmio_id, io_device_type_);
    // TODO: Remove this once we have proper mutex usage.
    lock_manager.initialize_mutex(MutexType::ARC_MSG);
}

xy_pair ArcMessenger::get_arc_core(const NocId noc_id) const {
    return noc_id == NocId::NOC1 ? arc_core_noc1_ : arc_core_noc0_;
}

uint32_t ArcMessenger::send_message(
    const uint32_t msg_code, const std::vector<uint32_t>& args, const std::chrono::milliseconds timeout_ms) {
    std::vector<uint32_t> return_values;
    return send_message(msg_code, return_values, args, timeout_ms);
}

ArcMessenger::~ArcMessenger() {
    lock_manager.clear_mutex(MutexType::ARC_MSG, mmio_id, io_device_type_);
    lock_manager.clear_mutex(MutexType::REMOTE_ARC_MSG, mmio_id, io_device_type_);
}

}  // namespace tt::umd
