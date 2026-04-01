// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace YAML {
class Node;
}

namespace tt::umd {

//! CoreDescriptor contains information regarding a single core on the SOC.
struct CoreDescriptor {
    tt_xy_pair coord = tt_xy_pair(0, 0);
    CoreType type;

    std::size_t l1_size = 0;
};

// SocArchDescriptor contains the static, architecture-determined properties of a chip.
// This data is identical for every chip of a given architecture and does not depend on
// per-chip runtime state like harvesting masks or NOC translation settings.
class SocArchDescriptor {
public:
    // Create from architecture enum (uses hardcoded constants).
    static SocArchDescriptor create(tt::ARCH arch);

    // Create from a YAML device descriptor file.
    static SocArchDescriptor create(const std::string& device_descriptor_path);

    // Helpers for extracting info from a YAML descriptor file without fully constructing.
    static tt::ARCH get_arch_from_path(const std::string& soc_descriptor_path);
    static tt_xy_pair get_grid_size_from_path(const std::string& soc_descriptor_path);

    // Calculate grid size from a list of cores.
    static tt_xy_pair calculate_grid_size(const std::vector<tt_xy_pair>& cores);

    // Architecture.
    tt::ARCH arch;

    // Full NOC grid size (unharvested).
    tt_xy_pair grid_size;

    // Core locations in NOC0 coordinates.
    std::vector<tt_xy_pair> tensix_cores;
    std::vector<std::vector<tt_xy_pair>> dram_cores;  // Per-channel list.
    std::vector<tt_xy_pair> eth_cores;
    std::vector<tt_xy_pair> arc_cores;
    std::vector<tt_xy_pair> pcie_cores;
    std::vector<tt_xy_pair> router_cores;
    std::vector<tt_xy_pair> security_cores;
    std::vector<tt_xy_pair> l2cpu_cores;
    std::vector<tt_xy_pair> dispatch_cores;

    // Memory sizes.
    uint32_t worker_l1_size = 0;
    uint32_t eth_l1_size = 0;
    uint64_t dram_bank_size = 0;

    // NOC0 to NOC1 coordinate translation tables.
    std::vector<uint32_t> noc0_x_to_noc1_x;
    std::vector<uint32_t> noc0_y_to_noc1_y;

    // Feature versions (populated from YAML; zero when created from arch enum).
    int overlay_version = 0;
    int unpacker_version = 0;
    int dst_size_alignment = 0;
    int packer_version = 0;

    // TRISC sizes.
    std::vector<std::size_t> trisc_sizes;

    // Path to the source YAML descriptor file (empty when created from arch enum).
    std::string device_descriptor_file_path;

    // Derived data, computed at construction time.

    // Core descriptor map (NOC0 coord -> CoreDescriptor).
    std::unordered_map<tt_xy_pair, CoreDescriptor> cores;

    // Worker grid size (computed from tensix core locations).
    tt_xy_pair worker_grid_size;

    // Channel maps.
    std::unordered_map<tt_xy_pair, std::tuple<int, int>> dram_core_channel_map;
    std::unordered_map<tt_xy_pair, int> ethernet_core_channel_map;

private:
    SocArchDescriptor() = default;

    // Build derived data (cores map, channel maps, worker_grid_size) from core location vectors.
    void build_derived_data();

    // Load from YAML node.
    void load_from_yaml(YAML::Node& device_descriptor_yaml);

    // Helper for YAML parsing.
    static std::vector<tt_xy_pair> convert_to_tt_xy_pair(const std::vector<std::string>& core_strings);
    static std::vector<std::vector<tt_xy_pair>> convert_dram_cores_from_yaml(
        YAML::Node& device_descriptor_yaml, const std::string& dram_core = "dram");
};

}  // namespace tt::umd
