/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>

#include <chrono>

#include "umd/device/cluster.hpp"

using namespace tt::umd;

/**
 * Measure the time it takes to open/construct a Cluster object with default ClusterOptions.
 */
TEST(MicrobenchmarkOpenCluster, ClusterConstructor) {
    ankerl::nanobench::Bench()
        .maxEpochTime(std::chrono::seconds(30))
        .title("Cluster creation")
        .timeUnit(std::chrono::milliseconds(1), "ms")
        .unit("clusters")
        .run([&] {
            std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
            ankerl::nanobench::doNotOptimizeAway(cluster);
        });
}
