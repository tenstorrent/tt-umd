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

// Wormhole-only broadcast over ethernet (ERISC FW). The header generation and coordinate adjustment are
// hardwired to Wormhole's grid layout and translated<->virtual tables, so this class must only be constructed
// for WORMHOLE_B0 clusters (the owning Cluster gates this on arch). Other architectures would silently
// misroute writes and are not supported here.
class EthernetBroadcast {
public:
    EthernetBroadcast(
        const std::unordered_map<ChipId, EthCoord>& chip_locations,
        const std::unordered_map<ChipId, ChipId>& chip_to_mmio_chip,
        const std::unordered_map<ChipId, RemoteCommunication*>& mmio_remote_comms);

    // A constructor variant to be used for a single remote chip.
    // Note that the chips_to_exclude parameter should be empty in case this EthernetBroadcast is used for a single
    // remote chip.
    EthernetBroadcast(RemoteCommunication* mmio_remote_comms);

    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<ChipId>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude,
        bool use_translated_coords);

    // Re-seeds all topology state captured at construction (chip locations, chip->MMIO mapping and the
    // per-MMIO RemoteCommunication pointers) and drops the cached broadcast headers. Takes the same three
    // pieces as the constructor so they cannot drift out of sync. Must be called whenever the owning Cluster
    // refreshes its cluster description, including when remote_communications_ is rebuilt, to avoid retaining
    // dangling RemoteCommunication* pointers.
    void refresh(
        const std::unordered_map<ChipId, EthCoord>& chip_locations,
        const std::unordered_map<ChipId, ChipId>& chip_to_mmio_chip,
        const std::unordered_map<ChipId, RemoteCommunication*>& mmio_remote_comms);

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

    bool single_remote_chip_ = false;

    std::unordered_map<ChipId, EthCoord> chip_locations_;
    std::unordered_map<ChipId, ChipId> chip_to_mmio_chip_;
    std::unordered_map<ChipId, RemoteCommunication*> mmio_remote_comms_;
    std::map<std::set<ChipId>, std::unordered_map<ChipId, std::vector<std::vector<int>>>> bcast_header_cache_;
};

}  // namespace tt::umd
