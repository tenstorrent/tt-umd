// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Ethernet broadcast reaches remote Wormhole chips, which this build does not support: it refuses construction.

#include "tt-umd/tt_device/ethernet_broadcast.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

[[noreturn]] void throw_ethernet_broadcast_not_supported() {
    UMD_THROW(error::RuntimeError, "Ethernet broadcast is not supported.");
}

}  // namespace

EthernetBroadcast::EthernetBroadcast(
    const std::unordered_map<ChipId, EthCoord>&,
    const std::unordered_map<ChipId, ChipId>&,
    const std::unordered_map<ChipId, RemoteCommunication*>&) {
    throw_ethernet_broadcast_not_supported();
}

EthernetBroadcast::EthernetBroadcast(RemoteCommunication*) { throw_ethernet_broadcast_not_supported(); }

void EthernetBroadcast::broadcast_write_to_cluster(
    const void*, uint32_t, uint64_t, const std::set<ChipId>&, std::set<uint32_t>&, std::set<uint32_t>&, bool) {
    throw_ethernet_broadcast_not_supported();
}

void EthernetBroadcast::refresh(
    const std::unordered_map<ChipId, EthCoord>&,
    const std::unordered_map<ChipId, ChipId>&,
    const std::unordered_map<ChipId, RemoteCommunication*>&) {
    throw_ethernet_broadcast_not_supported();
}

}  // namespace tt::umd
