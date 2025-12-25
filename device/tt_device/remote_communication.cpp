// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/remote_communication.hpp"

#include <tt-logger/tt-logger.hpp>

#include "umd/device/chip/local_chip.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/topology/topology_utils.hpp"
#include "umd/device/tt_device/remote_communication_legacy_firmware.hpp"
#include "umd/device/utils/assert.hpp"
#include "umd/device/utils/common.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

RemoteCommunication::RemoteCommunication(TTDevice* local_tt_device, SysmemManager* sysmem_manager) :
    local_tt_device_(local_tt_device), sysmem_manager_(sysmem_manager) {
    lock_manager_.initialize_mutex(MutexType::NON_MMIO, local_tt_device->get_communication_device_id());
}

std::unique_ptr<RemoteCommunication> RemoteCommunication::create_remote_communication(
    TTDevice* local_tt_device, EthCoord target_chip, SysmemManager* sysmem_manager) {
    switch (local_tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<RemoteCommunicationLegacyFirmware>(local_tt_device, target_chip, sysmem_manager);
        case tt::ARCH::BLACKHOLE:
            return nullptr;
        default:
            UMD_THROW("Remote communication is not supported for this architecture.");
    }
}

void RemoteCommunication::set_remote_transfer_ethernet_cores(
    const std::unordered_set<tt_xy_pair>& remote_transfer_eth_cores) {
    // Makes UMD aware of which ethernet cores have active links.
    // Based on this information, UMD determines which ethernet cores can be used for host->cluster non-MMIO transfers.
    // This overrides the default ethernet cores tagged for host to cluster routing in the constructor and must be
    // called for all MMIO devices, if default behaviour is not desired.
    remote_transfer_eth_cores_.assign(remote_transfer_eth_cores.begin(), remote_transfer_eth_cores.end());
}

TTDevice* RemoteCommunication::get_local_device() { return local_tt_device_; }

tt_xy_pair RemoteCommunication::get_remote_transfer_ethernet_core() {
    if (remote_transfer_eth_cores_.size() > 8) {
        // We cannot use more than 8 cores for umd access in one direction. Thats because of the available buffering in
        // the outgoing eth channels.
        log_warning(
            LogUMD, "Number of active ethernet cores {} exceeds the maximum of 8.", remote_transfer_eth_cores_.size());
    }
    if (remote_transfer_eth_cores_.empty()) {
        UMD_THROW("No remote transfer Ethernet cores set.");
    }
    return remote_transfer_eth_cores_.at(active_eth_core_idx);
}

void RemoteCommunication::update_active_eth_core_idx() {
    if (remote_transfer_eth_cores_.empty()) {
        UMD_THROW("Cannot update active Ethernet core index: no remote transfer Ethernet cores set.");
    }
    active_eth_core_idx = (active_eth_core_idx + 1) % remote_transfer_eth_cores_.size();
}

}  // namespace tt::umd
