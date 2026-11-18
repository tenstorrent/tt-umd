// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <fmt/format.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "tt-umd/types/arch.hpp"

namespace tt::umd::test_utils {

// Descriptors are located relative to the working directory (the repo root), which keeps test binaries relocatable.
// UMD_DESCRIPTORS_PATH overrides the descriptors directory when the binaries run away from a checkout.
inline std::string get_descriptor_path(const std::string& relative_path) {
    const char* env_root = std::getenv("UMD_DESCRIPTORS_PATH");
    const std::filesystem::path descriptors_root = env_root ? env_root : "descriptors";
    return (descriptors_root / relative_path).string();
}

inline std::string get_soc_desc_path(const std::string& soc_desc_name) {
    return get_descriptor_path("soc_descs/" + soc_desc_name);
}

inline std::string get_cluster_desc_path(const std::string& cluster_desc_name) {
    return get_descriptor_path("cluster_descriptor_examples/" + cluster_desc_name);
}

inline std::vector<std::string> get_all_cluster_descs() {
    std::vector<std::string> cluster_desc_names;
    for (const auto& cluster_desc_name : {
             "2x2_n300_cluster_desc.yaml",
             "6u_cluster_desc.yaml",
             "blackhole_8xP150.yaml",
             "blackhole_4xP150.yaml",
             "blackhole_galaxy.yaml",
             "blackhole_P100.yaml",
             "blackhole_P150.yaml",
             "blackhole_P300_first_mmio.yaml",
             "blackhole_P300_second_mmio.yaml",
             "blackhole_P300_both_mmio.yaml",
             "t3k_cluster_desc.yaml",
             "wormhole_2xN300_unconnected.yaml",
             "wormhole_N150_unique_ids.yaml",
             "wormhole_N150.yaml",
             "wormhole_N300_routing_info.yaml",
             "wormhole_N300_board_info.yaml",
             "wormhole_N300_with_remote_connections.yaml",
             "wormhole_N300_with_bus_id.yaml",
             "wormhole_N300.yaml",
             "wormhole_N300_jtag.yaml",
             "wormhole_N300_pci_bdf.yaml",
             "quasar_Q1.yaml",
         }) {
        cluster_desc_names.push_back(get_cluster_desc_path(cluster_desc_name));
    }
    return cluster_desc_names;
}

inline std::vector<std::string> get_all_soc_descs() {
    std::vector<std::string> soc_desc_names;
    for (const auto& soc_desc_name : {
             "blackhole_140_arch_no_eth.yaml",
             "blackhole_140_arch_no_noc1.yaml",
             "blackhole_140_arch.yaml",
             "blackhole_simulation_1x2.yaml",
             "quasar_simulation_1x1.yaml",
             "wormhole_b0_1x1.yaml",
             "wormhole_b0_8x10.yaml",
             "wormhole_b0_one_dram_one_tensix_no_eth.yaml",
             "quasar_32_arch.yaml",
             "quasar_simulation_1x3.yaml",
             "quasar_simulation_2x3.yaml",
             "quasar_simulation_9x4.yaml",
         }) {
        soc_desc_names.push_back(get_soc_desc_path(soc_desc_name));
    }
    return soc_desc_names;
}

inline std::string get_soc_descriptor_path(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return get_descriptor_path("soc_descs/wormhole_b0_8x10.yaml");
        case tt::ARCH::BLACKHOLE:
            return get_descriptor_path("soc_descs/blackhole_140_arch.yaml");
        case tt::ARCH::QUASAR:
            return get_descriptor_path("soc_descs/quasar_simulation_1x1.yaml");
        default:
            throw std::runtime_error("Invalid architecture");
    }
}

}  // namespace tt::umd::test_utils
