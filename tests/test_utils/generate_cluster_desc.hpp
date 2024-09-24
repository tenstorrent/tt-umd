/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <filesystem>
#include <string>
#include <iostream>

namespace test_utils {

inline std::string GetAbsPath(std::string path_){
    std::filesystem::path umd_root_relative = std::filesystem::relative(std::filesystem::path(__FILE__).parent_path().parent_path().parent_path(), "../");
        std::cout << "during compilation path was " << std::filesystem::path(__FILE__).string() << std::endl;
        std::cout << "umd_root_relative " << umd_root_relative.string() << std::endl;
    std::filesystem::path umd_root = std::filesystem::canonical(umd_root_relative);
        std::cout << "umd_root " << umd_root.string() << std::endl;
    std::filesystem::path abs_path = umd_root / path_;
        std::cout << "abs_path " << abs_path.string() << std::endl;
        std::cout << "path_ " << path_ << std::endl;
    return abs_path.string();
}

inline std::string GetClusterDescYAML(){
    static std::string yaml_path;
    static bool is_initialized = false;
    if (!is_initialized){
        std::filesystem::path umd_path = std::filesystem::path(test_utils::GetAbsPath(""));
        std::filesystem::path cluster_path = umd_path / ".umd";
        std::filesystem::create_directories( cluster_path );
        
        cluster_path /= "cluster_desc.yaml";
        if (!std::filesystem::exists(cluster_path)){
            auto val = system ( ("touch " + cluster_path.string()).c_str());
            if(val != 0) throw std::runtime_error("Cluster Generation Failed!");
        }
        // Generates the cluster descriptor in the CWD

        std::cout << "umd_path " << umd_path.string() << std::endl;
        std::filesystem::path eth_fpath = umd_path / "device/bin/silicon/x86/create-ethernet-map";
        std::string cmd = eth_fpath.string() + " " + cluster_path.string();
        int val = system(cmd.c_str());
        if(val != 0) throw std::runtime_error("Cluster Generation Failed!");
        yaml_path = cluster_path.string();
        is_initialized = true;
    }
    return yaml_path;
}
} // namespace test_utils
