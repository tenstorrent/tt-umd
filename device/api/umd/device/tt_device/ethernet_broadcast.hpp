// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class RemoteCommunication;

class EthernetBroadcast {
public:
    EthernetBroadcast(
        const std::unordered_map<ChipId, EthCoord>& chip_locations,
        const std::unordered_map<ChipId, ChipId>& chip_to_mmio_chip,
        const std::unordered_map<ChipId, RemoteCommunication*>& mmio_remote_comms);

    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<ChipId>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude,
        bool use_translated_coords);

    void clear_header_cache(
        const std::unordered_map<ChipId, EthCoord>& chip_locations,
        const std::unordered_map<ChipId, ChipId>& chip_to_mmio_chip);

private:
    // Validates that the caller-supplied rows/columns lie in the coordinate space selected by
    // @p use_translated_coords (NOC0 when false, translated-index when true) and emits their
    // virtual-space equivalents used by the ethernet broadcast path.
    static void adjust_coordinates_for_ethernet_broadcast(
        const std::set<uint32_t>& rows_to_exclude,
        const std::set<uint32_t>& columns_to_exclude,
        bool use_translated_coords,
        std::set<uint32_t>& rows_to_exclude_virtual,
        std::set<uint32_t>& cols_to_exclude_virtual);

    // Communication Functions.
    void ethernet_broadcast_write(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<ChipId>& chips_to_exclude,
        const std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& cols_to_exclude,
        bool use_translated_coords);

    std::unordered_map<ChipId, std::vector<std::vector<int>>>& get_ethernet_broadcast_headers(
        const std::set<ChipId>& chips_to_exclude);

    std::unordered_map<ChipId, EthCoord> chip_locations_;
    std::unordered_map<ChipId, ChipId> chip_to_mmio_chip_;
    std::unordered_map<ChipId, RemoteCommunication*> mmio_remote_comms_;
    std::map<std::set<ChipId>, std::unordered_map<ChipId, std::vector<std::vector<int>>>> bcast_header_cache_;
};

}  // namespace tt::umd
