// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt {
enum class CoreType;
}  // namespace tt

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
    virtual ~SocArchDescriptor() = default;

    // Create from architecture enum (uses hardcoded constants).
    SocArchDescriptor(tt::ARCH arch);

    // Create from a YAML SoC descriptor file.
    SocArchDescriptor(const std::string& soc_descriptor_path);

    // Populates the descriptor with the topology data selected at construction (architecture
    // constants or the YAML file) and rebuilds the derived data. Called by the constructors;
    // exposed as a virtual hook so future TTDeviceModel compositions can override population.
    virtual void init();

    // Helpers for extracting info from a YAML descriptor file without fully constructing.
    static tt::ARCH get_arch_from_path(const std::string& soc_descriptor_path);
    static tt_xy_pair get_grid_size_from_path(const std::string& soc_descriptor_path);

    // Calculate grid size from a list of cores.
    static tt_xy_pair calculate_grid_size(const std::vector<tt_xy_pair>& cores);

    // Architecture.
    tt::ARCH get_arch() const { return arch_; }

    // Full NOC grid size (unharvested).
    const tt_xy_pair& get_grid_size() const { return grid_size_; }

    // Core locations in NOC0 coordinates.
    const std::vector<tt_xy_pair>& get_tensix_cores() const { return tensix_cores_; }

    const std::vector<std::vector<tt_xy_pair>>& get_dram_cores() const { return dram_cores_; }

    const std::vector<tt_xy_pair>& get_eth_cores() const { return eth_cores_; }

    const std::vector<tt_xy_pair>& get_firmware_cores() const { return firmware_cores_; }

    const std::vector<tt_xy_pair>& get_pcie_cores() const { return pcie_cores_; }

    const std::vector<tt_xy_pair>& get_router_cores() const { return router_cores_; }

    const std::vector<tt_xy_pair>& get_security_cores() const { return security_cores_; }

    const std::vector<tt_xy_pair>& get_l2cpu_cores() const { return l2cpu_cores_; }

    const std::vector<tt_xy_pair>& get_dispatch_cores() const { return dispatch_cores_; }

    // Memory sizes.
    uint32_t get_worker_l1_size() const { return worker_l1_size_; }

    uint32_t get_eth_l1_size() const { return eth_l1_size_; }

    uint64_t get_dram_bank_size() const { return dram_bank_size_; }

    // NOC0 to NOC1 coordinate translation tables.
    const std::vector<uint32_t>& get_noc0_x_to_noc1_x() const { return noc0_x_to_noc1_x_; }

    const std::vector<uint32_t>& get_noc0_y_to_noc1_y() const { return noc0_y_to_noc1_y_; }

    // Feature versions (populated from YAML; zero when created from arch enum).
    int get_overlay_version() const { return overlay_version_; }

    int get_unpacker_version() const { return unpacker_version_; }

    int get_dst_size_alignment() const { return dst_size_alignment_; }

    int get_packer_version() const { return packer_version_; }

    // TRISC sizes.
    const std::vector<std::size_t>& get_trisc_sizes() const { return trisc_sizes_; }

    // Path to the source YAML descriptor file (empty when created from arch enum).
    const std::string& get_device_descriptor_file_path() const { return device_descriptor_file_path_; }

    // Derived data, computed at construction time.

    // Core descriptor map (NOC0 coord -> CoreDescriptor).
    const std::unordered_map<tt_xy_pair, CoreDescriptor>& get_cores() const { return cores_; }

    // Worker grid size (computed from tensix core locations).
    const tt_xy_pair& get_worker_grid_size() const { return worker_grid_size_; }

    // Channel maps.
    const std::unordered_map<tt_xy_pair, std::tuple<int, int>>& get_dram_core_channel_map() const {
        return dram_core_channel_map_;
    }

    const std::unordered_map<tt_xy_pair, int>& get_ethernet_core_channel_map() const {
        return ethernet_core_channel_map_;
    }

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

    tt::ARCH arch_;
    tt_xy_pair grid_size_;
    std::vector<tt_xy_pair> tensix_cores_;
    std::vector<std::vector<tt_xy_pair>> dram_cores_;
    std::vector<tt_xy_pair> eth_cores_;
    std::vector<tt_xy_pair> firmware_cores_;
    std::vector<tt_xy_pair> pcie_cores_;
    std::vector<tt_xy_pair> router_cores_;
    std::vector<tt_xy_pair> security_cores_;
    std::vector<tt_xy_pair> l2cpu_cores_;
    std::vector<tt_xy_pair> dispatch_cores_;
    uint32_t worker_l1_size_ = 0;
    uint32_t eth_l1_size_ = 0;
    uint64_t dram_bank_size_ = 0;
    std::vector<uint32_t> noc0_x_to_noc1_x_;
    std::vector<uint32_t> noc0_y_to_noc1_y_;
    int overlay_version_ = 0;
    int unpacker_version_ = 0;
    int dst_size_alignment_ = 0;
    int packer_version_ = 0;
    std::vector<std::size_t> trisc_sizes_;
    std::string device_descriptor_file_path_;
    std::unordered_map<tt_xy_pair, CoreDescriptor> cores_;
    tt_xy_pair worker_grid_size_;
    std::unordered_map<tt_xy_pair, std::tuple<int, int>> dram_core_channel_map_;
    std::unordered_map<tt_xy_pair, int> ethernet_core_channel_map_;
};

}  // namespace tt::umd
