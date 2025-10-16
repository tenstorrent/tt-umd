/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>
#include <sys/mman.h>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt::umd;

/**
 * Measure the time it takes to open/construct a Cluster object with default ClusterOptions.
 */
TEST(MicrobenchmarkOpenCluster, ClusterConstructor) {
    const std::vector<std::string> headers = {"Opening cluster of devices (ms)"};

    auto now = std::chrono::steady_clock::now();
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    auto end = std::chrono::steady_clock::now();

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    row.push_back(test::utils::convert_double_to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count() / (double)1e6));
    rows.push_back(row);

    test::utils::print_markdown_table_format(headers, rows);
}
