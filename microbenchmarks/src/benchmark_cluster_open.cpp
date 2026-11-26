// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "tt-umd-workload/cluster.hpp"
#include "utils.hpp"

using namespace tt::umd;
using namespace tt::umd::microbenchmarks;

// The cost every tool and workload pays before its first IO: discovering the topology, and opening the cluster, which
// includes the discovery.
TEST(ClusterOpen, Speed) {
    if (Cluster(ClusterOptions{}).get_target_device_ids().empty()) {
        GTEST_SKIP() << "No devices found in the cluster.";
    }

    const std::vector<std::string> context_variables = {"operation"};
    Bench bench;
    bench.title("ClusterOpen").unit("op").performanceCounters(false).warmup(1);

    auto benchmark = [&](const std::string& operation, auto run) {
        bench.context("operation", operation);
        bench.run(fmt::format("operation: {}", operation), run);
    };

    benchmark("topology_discovery", [] { doNotOptimizeAway(Cluster::create_cluster_descriptor()); });
    benchmark("open_and_close", [] { Cluster cluster(ClusterOptions{}); });
    export_results(bench, context_variables);
}
