// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// RemoteCommunication surface is intentionally left unimplemented in the mock.
// These code paths will never be hit in the mock environment and are not meant
// to be implemented here.

#include "tt-umd/tt_device/remote_communication.hpp"

namespace tt::umd {

std::unique_ptr<RemoteCommunication> RemoteCommunication::create_remote_communication(
    TTDevice*, EthCoord, SysmemManager*) {
    return nullptr;
}

void RemoteCommunication::set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>&) {}

}  // namespace tt::umd
