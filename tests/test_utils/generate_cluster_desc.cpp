/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tests/test_utils/generate_cluster_desc.hpp"
#include "device/tt_device.h"

namespace test_utils {
fs::path generate_cluster_desc_yaml() {
    fs::path umd_path = fs::path ( __FILE__ ).parent_path().parent_path() / ".umd";
    fs::create_directory( umd_path );
    umd_path /= "cluster_desc.yaml";
    if (!fs::exists(umd_path)){
        auto val = system ( ("touch " + umd_path.string()).c_str());
        if(val != 0) throw std::runtime_error("Cluster Generation Failed!");
    }
    // Generates the cluster descriptor in the CWD

    fs::path eth_fpath = fs::path ( __FILE__ ).parent_path().parent_path().parent_path();
    eth_fpath /= "device/bin/silicon/x86/create-ethernet-map";
    std::string cmd = eth_fpath.string() + " " + umd_path.string();
    int val = system(cmd.c_str());
    if(val != 0) throw std::runtime_error("Cluster Generation Failed!");

    return fs::absolute(umd_path);
}
}

fs::path GetClusterDescYAML()
{
    static fs::path yaml_path{test_utils::generate_cluster_desc_yaml()};
    return yaml_path;
}

