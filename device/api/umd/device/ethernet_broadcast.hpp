// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {

class Cluster;

class EthernetBroadcast {
    friend class Cluster;

public:
    EthernetBroadcast(Cluster& cluster, bool use_translated_coords_for_eth_broadcast);

    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<ChipId>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude);

    void clear_header_cache();

private:
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

    Cluster& cluster_;
    std::map<std::set<ChipId>, std::unordered_map<ChipId, std::vector<std::vector<int>>>> bcast_header_cache_;
    bool use_translated_coords_for_eth_broadcast_;
};

}  // namespace tt::umd
