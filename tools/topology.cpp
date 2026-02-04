// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cxxopts.hpp>
#include <thread>
#include <tracy/Tracy.hpp>
#include <tt-logger/tt-logger.hpp>
#define TRACY_ENABLE 1
#include "common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"

using namespace tt::umd;

int main(int argc, char *argv[]) {
    while (true) {
        std::unique_ptr<ClusterDescriptor> cluster_descriptor = Cluster::create_cluster_descriptor();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
