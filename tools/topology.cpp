// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "logger.hpp"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

using namespace tt::umd;

int main(int argc, char *argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: topology <cluster_descriptor_path>" << std::endl;
        return 1;
    }

    std::string cluster_descriptor_path = "";
    if (argc == 2) {
        cluster_descriptor_path = std::string(argv[1]);
    }

    std::string output_path = Cluster::serialize_to_file(cluster_descriptor_path);
    log_info(tt::LogSiliconDriver, "Cluster descriptor serialized to {}", output_path);
    return 0;
}
