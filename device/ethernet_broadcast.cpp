// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/ethernet_broadcast.hpp"

#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "assert.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/cluster.hpp"

namespace tt::umd {

namespace {

inline bool tensix_or_eth_in_broadcast(
    const std::set<uint32_t>& cols_to_exclude, const architecture_implementation* architecture_implementation) {
    bool found_tensix_or_eth = false;
    for (const auto& col : architecture_implementation->get_t6_x_locations()) {
        found_tensix_or_eth |= (cols_to_exclude.find(col) == cols_to_exclude.end());
    }
    return found_tensix_or_eth;
}

inline bool valid_tensix_broadcast_grid(
    const std::set<uint32_t>& rows_to_exclude,
    const std::set<uint32_t>& cols_to_exclude,
    const architecture_implementation* architecture_implementation) {
    bool t6_bcast_rows_complete = true;
    bool t6_bcast_rows_empty = true;

    for (const auto& row : architecture_implementation->get_t6_y_locations()) {
        t6_bcast_rows_complete &= (rows_to_exclude.find(row) == rows_to_exclude.end());
        t6_bcast_rows_empty &= (rows_to_exclude.find(row) != rows_to_exclude.end());
    }
    return t6_bcast_rows_complete || t6_bcast_rows_empty;
}

}  // namespace

EthernetBroadcast::EthernetBroadcast(Cluster& cluster, bool use_translated_coords_for_eth_broadcast) :
    cluster_(cluster), use_translated_coords_for_eth_broadcast_(use_translated_coords_for_eth_broadcast) {}

void EthernetBroadcast::clear_header_cache() { bcast_header_cache_.clear(); }

std::unordered_map<ChipId, std::vector<std::vector<int>>>& EthernetBroadcast::get_ethernet_broadcast_headers(
    const std::set<ChipId>& chips_to_exclude) {
    // Generate headers for Ethernet Broadcast (WH) only. Each header corresponds to a unique broadcast "grid".
    if (bcast_header_cache_.find(chips_to_exclude) == bcast_header_cache_.end()) {
        bcast_header_cache_[chips_to_exclude] = {};
        std::unordered_map<ChipId, std::unordered_map<ChipId, std::vector<int>>>
            broadcast_mask_for_target_chips_per_group = {};
        std::map<std::vector<int>, std::tuple<ChipId, std::vector<int>>> broadcast_header_union_per_group = {};
        ChipId first_mmio_chip = *(cluster_.get_target_mmio_device_ids().begin());
        for (const auto& chip : cluster_.all_chip_ids_) {
            if (chips_to_exclude.find(chip) == chips_to_exclude.end()) {
                // Get shelf local physical chip id included in broadcast.
                ChipId physical_chip_id = cluster_.cluster_desc->get_shelf_local_physical_chip_coords(chip);
                EthCoord eth_coords = cluster_.cluster_desc->get_chip_locations().at(chip);
                // Rack word to be set in header.
                uint32_t rack_word = eth_coords.rack >> 2;
                // Rack byte to be set in header.
                uint32_t rack_byte = eth_coords.rack % 4;
                // 1st level grouping: Group broadcasts based on the MMIO chip they must go through
                // Nebula + Galaxy Topology assumption: Disjoint sets can only be present in the first shelf, with each
                // set connected to host through its closest MMIO chip For the first shelf, pass broadcasts to specific
                // chips through their closest MMIO chip All other shelves are fully connected galaxy grids. These are
                // connected to all MMIO devices. Use any (or the first) MMIO device in the list.
                ChipId closest_mmio_chip = 0;
                if (eth_coords.rack == 0 && eth_coords.shelf == 0) {
                    // Shelf 0 + Rack 0: Either an MMIO chip or a remote chip potentially connected to host through its
                    // own MMIO counterpart.
                    closest_mmio_chip = cluster_.cluster_desc->get_closest_mmio_capable_chip(chip);
                } else {
                    // All other shelves: Group these under the same/first MMIO chip, since all MMIO chips are
                    // connected.
                    closest_mmio_chip = first_mmio_chip;
                }
                if (broadcast_mask_for_target_chips_per_group.find(closest_mmio_chip) ==
                    broadcast_mask_for_target_chips_per_group.end()) {
                    broadcast_mask_for_target_chips_per_group.insert({closest_mmio_chip, {}});
                }
                // For each target physical chip id (local to a shelf), generate headers based on all racks and shelves
                // that contain this physical id.
                if (broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).find(physical_chip_id) ==
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip).end()) {
                    // Target seen for the first time.
                    std::vector<int> broadcast_mask(8, 0);
                    broadcast_mask.at(rack_word) |= (1 << eth_coords.shelf) << rack_byte;
                    broadcast_mask.at(3) |= 1 << physical_chip_id;
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip)
                        .insert({physical_chip_id, broadcast_mask});

                } else {
                    // Target was seen before -> include curr rack and shelf in header.
                    broadcast_mask_for_target_chips_per_group.at(closest_mmio_chip)
                        .at(physical_chip_id)
                        .at(rack_word) |= static_cast<uint32_t>(1 << eth_coords.shelf) << rack_byte;
                }
            }
        }
        // 2nd level grouping: For each MMIO group, further group the chips based on their rack and shelf headers. The
        // number of groups after this step represent the final set of broadcast grids.
        for (auto& mmio_group : broadcast_mask_for_target_chips_per_group) {
            for (auto& chip : mmio_group.second) {
                // Generate a hash for this MMIO Chip + Rack + Shelf group.
                std::vector<int> header_hash = {
                    mmio_group.first, chip.second.at(0), chip.second.at(1), chip.second.at(2)};
                if (broadcast_header_union_per_group.find(header_hash) == broadcast_header_union_per_group.end()) {
                    broadcast_header_union_per_group.insert(
                        {header_hash, std::make_tuple(mmio_group.first, chip.second)});
                } else {
                    // If group found, update chip header entry.
                    std::get<1>(broadcast_header_union_per_group.at(header_hash)).at(3) |= chip.second.at(3);
                }
            }
        }
        // Get all broadcast headers per MMIO group.
        for (const auto& header : broadcast_header_union_per_group) {
            ChipId mmio_chip = std::get<0>(header.second);
            if (bcast_header_cache_[chips_to_exclude].find(mmio_chip) == bcast_header_cache_[chips_to_exclude].end()) {
                bcast_header_cache_[chips_to_exclude].insert({mmio_chip, {}});
            }
            bcast_header_cache_[chips_to_exclude].at(mmio_chip).push_back(std::get<1>(header.second));
        }
        // Invert headers (FW convention).
        for (auto& bcast_group : bcast_header_cache_[chips_to_exclude]) {
            for (auto& header : bcast_group.second) {
                int header_idx = 0;
                for (auto& header_entry : header) {
                    if (header_idx == 4) {
                        break;
                    }
                    header_entry = ~header_entry;
                    header_idx++;
                }
            }
        }
    }
    return bcast_header_cache_[chips_to_exclude];
}

void EthernetBroadcast::ethernet_broadcast_write(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<ChipId>& chips_to_exclude,
    const std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& cols_to_exclude,
    bool use_translated_coords) {
    // Broadcast through ERISC core supported.
    std::unordered_map<ChipId, std::vector<std::vector<int>>>& broadcast_headers =
        get_ethernet_broadcast_headers(chips_to_exclude);
    // Apply row and column exclusion mask explictly. Placing this here if we want to cache the higher level
    // broadcast headers on future/
    std::uint32_t row_exclusion_mask = 0;
    std::uint32_t col_exclusion_mask = 0;
    for (const auto& row : rows_to_exclude) {
        row_exclusion_mask |= 1 << row;
    }

    for (const auto& col : cols_to_exclude) {
        col_exclusion_mask |= 1 << (16 + col);
    }
    // Write broadcast block to device.
    for (auto& mmio_group : broadcast_headers) {
        for (auto& header : mmio_group.second) {
            header.at(4) = use_translated_coords * 0x8000;  // Reset row/col exclusion masks
            header.at(4) |= row_exclusion_mask;
            header.at(4) |= col_exclusion_mask;
            cluster_.get_local_chip(mmio_group.first)
                ->ethernet_broadcast_write(mem_ptr, address, size_in_bytes, header);
        }
    }
}

void EthernetBroadcast::broadcast_write_to_cluster(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    uint64_t address,
    const std::set<ChipId>& chips_to_exclude,
    std::set<uint32_t>& rows_to_exclude,
    std::set<uint32_t>& columns_to_exclude) {
    if (cluster_.arch_name == tt::ARCH::BLACKHOLE) {
        auto architecture_implementation = architecture_implementation::create(cluster_.arch_name);
        if (columns_to_exclude.find(0) == columns_to_exclude.end() or
            columns_to_exclude.find(9) == columns_to_exclude.end()) {
            TT_ASSERT(
                !tensix_or_eth_in_broadcast(columns_to_exclude, architecture_implementation.get()),
                "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Blackhole.");
            if (columns_to_exclude.find(0) == columns_to_exclude.end()) {
                // When broadcast includes column zero do not exclude anything.
                std::set<uint32_t> unsafe_rows = {};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = columns_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(9);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude_for_col_0_bcast,
                    cols_to_exclude_for_col_0_bcast,
                    false);
            }
            if (columns_to_exclude.find(9) == columns_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_9_bcast = columns_to_exclude;
                cols_to_exclude_for_col_9_bcast.insert(0);
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude,
                    cols_to_exclude_for_col_9_bcast,
                    false);
            }
        } else {
            TT_ASSERT(
                use_translated_coords_for_eth_broadcast_ or
                    valid_tensix_broadcast_grid(rows_to_exclude, columns_to_exclude, architecture_implementation.get()),
                "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(
                mem_ptr,
                size_in_bytes,
                address,
                chips_to_exclude,
                rows_to_exclude,
                columns_to_exclude,
                use_translated_coords_for_eth_broadcast_);
        }
    } else {
        auto architecture_implementation = architecture_implementation::create(cluster_.arch_name);
        if (columns_to_exclude.find(0) == columns_to_exclude.end() or
            columns_to_exclude.find(5) == columns_to_exclude.end()) {
            TT_ASSERT(
                !tensix_or_eth_in_broadcast(columns_to_exclude, architecture_implementation.get()),
                "Cannot broadcast to tensix/ethernet and DRAM simultaneously on Wormhole.");
            if (columns_to_exclude.find(0) == columns_to_exclude.end()) {
                // When broadcast includes column zero Exclude PCIe, ARC and router cores from broadcast explictly,
                // since writing to these is unsafe ERISC FW does not exclude these.
                std::set<uint32_t> unsafe_rows = {2, 3, 4, 8, 9, 10};
                std::set<uint32_t> cols_to_exclude_for_col_0_bcast = columns_to_exclude;
                std::set<uint32_t> rows_to_exclude_for_col_0_bcast = rows_to_exclude;
                cols_to_exclude_for_col_0_bcast.insert(5);
                rows_to_exclude_for_col_0_bcast.insert(unsafe_rows.begin(), unsafe_rows.end());
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude_for_col_0_bcast,
                    cols_to_exclude_for_col_0_bcast,
                    false);
            }
            if (columns_to_exclude.find(5) == columns_to_exclude.end()) {
                std::set<uint32_t> cols_to_exclude_for_col_5_bcast = columns_to_exclude;
                cols_to_exclude_for_col_5_bcast.insert(0);
                ethernet_broadcast_write(
                    mem_ptr,
                    size_in_bytes,
                    address,
                    chips_to_exclude,
                    rows_to_exclude,
                    cols_to_exclude_for_col_5_bcast,
                    false);
            }
        } else {
            TT_ASSERT(
                use_translated_coords_for_eth_broadcast_ or
                    valid_tensix_broadcast_grid(rows_to_exclude, columns_to_exclude, architecture_implementation.get()),
                "Must broadcast to all tensix rows when ERISC FW is < 6.8.0.");
            ethernet_broadcast_write(
                mem_ptr,
                size_in_bytes,
                address,
                chips_to_exclude,
                rows_to_exclude,
                columns_to_exclude,
                use_translated_coords_for_eth_broadcast_);
        }
    }
}

}  // namespace tt::umd
