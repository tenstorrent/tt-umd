/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "common/microbenchmark_utils.h"
#include "gtest/gtest.h"

using namespace tt::umd;

constexpr uint32_t one_mb = 1 << 20;
constexpr uint32_t NUM_ITERATIONS = 10;

/**
 * Measure BW of IO to DRAM core on the ETH connected device.
 */
TEST(MicrobenchmarkEthernetIO, DRAM) {
    // Sizes are chosen in a way to avoid TLB benchmark taking too long. 32 MB already
    // tests chunking of data into smaller chunks to match TLB size.
    // 64 MB and above showed the same perf locally.
    const std::array<uint32_t, 6> sizes = {
        1 * one_mb,
        2 * one_mb,
        4 * one_mb,
        8 * one_mb,
        16 * one_mb,
        32 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const chip_id_t chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device(tt_device_params{});

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> ETH Device DRAM (MB/s)",
        "ETH Device DRAM -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (uint32_t buf_size : sizes) {
        std::vector<std::string> row;
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, dram_core);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure BW of IO to Tensix core on ETH connected device.
 */
TEST(MicrobenchmarkEthernetIO, Tensix) {
    const std::array<uint32_t, 6> sizes = {
        1 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const chip_id_t chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device(tt_device_params{});

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> ETH Device Tensix L1 (MB/s)",
        "ETH Device Tensix L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (uint32_t buf_size : sizes) {
        std::vector<std::string> row;
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, tensix_core);
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}
