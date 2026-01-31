// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>

#include <chrono>
#include <memory>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/warm_reset.hpp"

using namespace tt::umd;
using namespace tt::umd::test::utils;

// Measure the time it takes to open/construct a Cluster object with default ClusterOptions.
TEST(MicrobenchmarkOpenCluster, ClusterConstructor) {
    auto bench = ankerl::nanobench::Bench()
                     .maxEpochTime(std::chrono::seconds(30))
                     .title("ClusterConstructor")
                     .unit("cluster")
                     .name("default")
                     .run([&] {
                         std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
                         ankerl::nanobench::doNotOptimizeAway(cluster);
                     });
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkOpenCluster, TopologyDiscovery) {
    auto bench =
        ankerl::nanobench::Bench().maxEpochTime(std::chrono::seconds(30)).title("TopologyDiscovery").unit("discovery");
    bench.name("default").run([&] {
        auto [cluster_descriptor, devices] = TopologyDiscovery::discover({});
        ankerl::nanobench::doNotOptimizeAway(devices);
    });
    bench.name("local only").run([&] {
        auto [cluster_descriptor, devices] = TopologyDiscovery::discover({.no_remote_discovery = true});
        ankerl::nanobench::doNotOptimizeAway(devices);
    });

    test::utils::export_results(bench);
}
