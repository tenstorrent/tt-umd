// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>

#include <chrono>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt::umd;
using namespace tt::umd::test::utils;

/**
 * Measure the time it takes to open/construct a Cluster object with default ClusterOptions.
 */
TEST(MicrobenchmarkOpenCluster, ClusterConstructor) {
    auto bench = ankerl::nanobench::Bench()
                     .maxEpochTime(std::chrono::seconds(30))
                     .title("ClusterConstructor")
                     .timeUnit(std::chrono::milliseconds(1), "ms")
                     .unit("cluster")
                     .name("default")
                     .run([&] {
                         std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
                         ankerl::nanobench::doNotOptimizeAway(cluster);
                     });
    test::utils::export_results(bench);
}
