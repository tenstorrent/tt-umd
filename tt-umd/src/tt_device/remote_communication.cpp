// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Remote chips are not supported by this build, so no RemoteCommunication can be created. The base-class
// bookkeeping is kept since it has no transport behind it.

#include "tt-umd/tt_device/remote_communication.hpp"

#include <algorithm>

#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

RemoteCommunication::RemoteCommunication(TTDevice* local_tt_device, SysmemManager* sysmem_manager) :
    local_tt_device_(local_tt_device), sysmem_manager_(sysmem_manager) {}

std::unique_ptr<RemoteCommunication> RemoteCommunication::create_remote_communication(
    TTDevice*, EthCoord, SysmemManager*) {
    UMD_THROW(error::RuntimeError, "Remote devices are not supported.");
}

void RemoteCommunication::set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>& cores) {
    remote_transfer_eth_cores_.assign(cores.begin(), cores.end());
    std::sort(remote_transfer_eth_cores_.begin(), remote_transfer_eth_cores_.end());
    active_eth_core_idx = 0;
}

const std::vector<tt_xy_pair>& RemoteCommunication::get_remote_transfer_ethernet_cores() const {
    return remote_transfer_eth_cores_;
}

TTDevice* RemoteCommunication::get_local_device() { return local_tt_device_; }

tt_xy_pair RemoteCommunication::get_remote_transfer_ethernet_core() {
    UMD_ASSERT(
        !remote_transfer_eth_cores_.empty(), error::RuntimeError, "No ethernet cores are set for remote transfers.");
    return remote_transfer_eth_cores_.at(active_eth_core_idx);
}

void RemoteCommunication::set_sysmem_manager(SysmemManager* sysmem_manager) { sysmem_manager_ = sysmem_manager; }

bool RemoteCommunication::has_sysmem_manager() const { return sysmem_manager_ != nullptr; }

void RemoteCommunication::update_active_eth_core_idx() {
    if (!remote_transfer_eth_cores_.empty()) {
        active_eth_core_idx = (active_eth_core_idx + 1) % remote_transfer_eth_cores_.size();
    }
}

}  // namespace tt::umd
