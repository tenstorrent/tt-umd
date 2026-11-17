// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/remote_communication.hpp"

#include <memory>
#include <unordered_set>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

RemoteCommunication::RemoteCommunication(TTDevice* local_tt_device, SysmemManager* sysmem_manager) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

std::unique_ptr<RemoteCommunication> RemoteCommunication::create_remote_communication(
    TTDevice* local_tt_device, EthCoord target_chip, SysmemManager* sysmem_manager) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

void RemoteCommunication::set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>& cores) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

const std::vector<tt_xy_pair>& RemoteCommunication::get_remote_transfer_ethernet_cores() const {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

TTDevice* RemoteCommunication::get_local_device() { UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access"); }

tt_xy_pair RemoteCommunication::get_remote_transfer_ethernet_core() {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

void RemoteCommunication::set_sysmem_manager(SysmemManager* sysmem_manager) {
    UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access");
}

bool RemoteCommunication::has_sysmem_manager() const { UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access"); }

void RemoteCommunication::update_active_eth_core_idx() { UMD_NOT_SUPPORTED("Remote (ETH-tunnelled) device access"); }

}  // namespace tt::umd
