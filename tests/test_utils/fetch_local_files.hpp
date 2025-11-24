/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/core.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace test_utils {

inline std::string GetAbsPath(std::string path_) {
    // Note that __FILE__ might be resolved at compile time to an absolute or relative address, depending on the
    // compiler.
    std::filesystem::path current_file_path = std::filesystem::path(__FILE__);
    std::filesystem::path umd_root;
    if (current_file_path.is_absolute()) {
        umd_root = current_file_path.parent_path().parent_path().parent_path();
    } else {
        std::filesystem::path umd_root_relative =
            std::filesystem::relative(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path(), "../");
        umd_root = std::filesystem::canonical(umd_root_relative);
    }
    std::filesystem::path abs_path = umd_root / path_;
    return abs_path.string();
}

inline std::string GetSocDescAbsPath(std::string soc_desc_name) {
    return GetAbsPath("tests/soc_descs/" + soc_desc_name);
}

inline std::string GetClusterDescAbsPath(std::string cluster_desc_name) {
    return GetAbsPath("tests/cluster_descriptor_examples/" + cluster_desc_name);
}

inline std::vector<std::string> GetAllClusterDescs() {
    std::vector<std::string> cluster_desc_names;
    for (const auto& cluster_desc_name : {
             "2x2_n300_cluster_desc.yaml",
             "6u_cluster_desc.yaml",
             "blackhole_P100.yaml",
             "blackhole_P150.yaml",
             "blackhole_P300_first_mmio.yaml",
             "blackhole_P300_second_mmio.yaml",
             "blackhole_P300_both_mmio.yaml",
             "t3k_cluster_desc.yaml",
             "tg_cluster_desc.yaml",
             "wormhole_2xN300_unconnected.yaml",
             "wormhole_4xN300_mesh.yaml",
             "wormhole_N150_unique_ids.yaml",
             "wormhole_N150.yaml",
             "wormhole_N300_routing_info.yaml",
             "wormhole_N300_board_info.yaml",
             "wormhole_N300_with_remote_connections.yaml",
             "wormhole_N300_with_bus_id.yaml",
             "wormhole_N300.yaml",
         }) {
        cluster_desc_names.push_back(GetClusterDescAbsPath(cluster_desc_name));
    }
    return cluster_desc_names;
}

inline std::vector<std::string> GetAllSocDescs() {
    std::vector<std::string> soc_desc_names;
    for (const auto& soc_desc_name : {
             "blackhole_140_arch_no_eth.yaml",
             "blackhole_140_arch_no_noc1.yaml",
             "blackhole_140_arch.yaml",
             "blackhole_simulation_1x2.yaml",
             "quasar_simulation_1x1.yaml",
             "serialized.yaml",
             "wormhole_b0_1x1.yaml",
             "wormhole_b0_8x10.yaml",
             "wormhole_b0_one_dram_one_tensix_no_eth.yaml",
         }) {
        soc_desc_names.push_back(GetSocDescAbsPath(soc_desc_name));
    }
    return soc_desc_names;
}

}  // namespace test_utils
