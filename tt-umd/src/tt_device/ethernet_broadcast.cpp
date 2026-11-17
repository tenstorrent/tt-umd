// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/ethernet_broadcast.hpp"

#include <cstdint>
#include <set>
#include <unordered_map>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

EthernetBroadcast::EthernetBroadcast(
    const std::unordered_map<ChipId, EthCoord>& chip_locations,
    const std::unordered_map<ChipId, ChipId>& chip_to_mmio_chip,
    const std::unordered_map<ChipId, RemoteCommunication*>& mmio_remote_comms) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

EthernetBroadcast::EthernetBroadcast(RemoteCommunication* mmio_remote_comms) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

void EthernetBroadcast::broadcast_write_to_cluster(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<ChipId>& chips_to_exclude,
    std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& columns_to_exclude,
    bool use_translated_coords) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

void EthernetBroadcast::refresh(
    const std::unordered_map<ChipId, EthCoord>& chip_locations,
    const std::unordered_map<ChipId, ChipId>& chip_to_mmio_chip,
    const std::unordered_map<ChipId, RemoteCommunication*>& mmio_remote_comms) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

void EthernetBroadcast::adjust_coordinates_for_ethernet_broadcast(
    const std::set<uint32_t>& rows_to_exclude,
    const std::set<uint32_t>& columns_to_exclude,
    bool use_translated_coords,
    std::set<uint32_t>& rows_to_exclude_virtual,
    std::set<uint32_t>& cols_to_exclude_virtual) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

void EthernetBroadcast::ethernet_broadcast_write(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<ChipId>& chips_to_exclude,
    const std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude,
    bool use_translated_coords) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

std::unordered_map<ChipId, std::vector<std::vector<int>>>& EthernetBroadcast::get_ethernet_broadcast_headers(
    const std::set<ChipId>& chips_to_exclude) {
    UMD_NOT_SUPPORTED("Ethernet broadcast");
}

}  // namespace tt::umd
