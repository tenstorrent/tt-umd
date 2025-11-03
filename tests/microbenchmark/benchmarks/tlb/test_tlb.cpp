/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>

#include "common/microbenchmark_utils.hpp"

using namespace tt;
using namespace tt::umd;

constexpr ChipId chip = 0;
constexpr size_t one_kb = 1 << 10;
constexpr size_t one_mb = 1 << 20;
constexpr size_t one_gb = 1ULL << 30;
constexpr uint32_t NUM_ITERATIONS = 10;
constexpr uint32_t tlb_1m_index = 0;
constexpr uint32_t tlb_16m_index = 166;

/**
 * Measure BW of IO to DRAM core using dynamically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBDynamicDram) {
    // Sizes are chosen in a way to avoid TLB benchmark taking too long. 32 MB already
    // tests chunking of data into smaller chunks to match TLB size.
    // 64 MB and above showed the same perf locally.
    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        1 * one_mb,
        2 * one_mb,
        4 * one_mb,
        8 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];

    const std::vector<std::string> headers = {
        "Size (bytes)",
        "Dynamic TLB: Host -> Device DRAM (MB/s)",
        "Dynamic TLB: Device DRAM -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (size_t buf_size : sizes) {
        std::vector<std::string> row;
        row.push_back(test::utils::convert_double_to_string((buf_size)));
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, dram_core);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure BW of IO to Tensix core using dynamically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBDynamicTensix) {
    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        1 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];

    const std::vector<std::string> headers = {
        "Size (bytes)",
        "Dynamic TLB: Host -> Device Tensix L1 (MB/s)",
        "Dynamic TLB: Device Tensix L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (size_t buf_size : sizes) {
        std::vector<std::string> row;
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, tensix_core);
        row.push_back(test::utils::convert_double_to_string(buf_size));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure BW of IO to Tensix core using statically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBStaticTensix) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];

    cluster->configure_tlb(0, tensix_core, tlb_1m_index, 0x0, tlb_data::Relaxed);

    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        1 * one_mb,
    };

    const std::vector<std::string> headers = {
        "Size (bytes)",
        "Static TLB: Host -> Device Tensix L1 (MB/s)",
        "Static TLB: Device Tensix L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (size_t buf_size : sizes) {
        std::vector<std::string> row;
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, tensix_core);
        row.push_back(test::utils::convert_double_to_string(buf_size));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure BW of IO to DRAM core using dynamically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBStaticDram) {
    // Sizes are chosen in a way to avoid TLB benchmark taking too long. 32 MB already
    // tests chunking of data into smaller chunks to match TLB size.
    // 64 MB and above showed the same perf locally.
    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        1 * one_mb,
        2 * one_mb,
        4 * one_mb,
        8 * one_mb,
        16 * one_mb,
        32 * one_mb};

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];

    cluster->configure_tlb(0, dram_core, tlb_16m_index, 0x0, tlb_data::Relaxed);

    const std::vector<std::string> headers = {
        "Size (bytes)",
        "Static TLB: Host -> Device DRAM (MB/s)",
        "Static TLB: Device DRAM -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (size_t buf_size : sizes) {
        std::vector<std::string> row;
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, dram_core);
        row.push_back(test::utils::convert_double_to_string(buf_size));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure BW of IO to Ethernet core using dynamically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBDynamicEth) {
    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        128 * one_kb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord eth_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::ETH)[0];

    const std::vector<std::string> headers = {
        "Size (bytes)",
        "Dynamic TLB: Host -> Device ETH L1 (MB/s)",
        "Dynamic TLB: Device ETH L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;
    constexpr uint32_t address = 128 * one_kb;
    for (size_t buf_size : sizes) {
        std::vector<std::string> row;
        auto [wr_bw, rd_bw] =
            test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, eth_core, address);
        row.push_back(test::utils::convert_double_to_string(buf_size));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure BW of IO to Eth core using statically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBStaticEth) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord eth_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::ETH)[0];

    constexpr uint32_t address = 128 * one_kb;
    cluster->configure_tlb(chip, eth_core, tlb_1m_index, address, tlb_data::Relaxed);

    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        128 * one_kb,
    };

    const std::vector<std::string> headers = {
        "Size (bytes)",
        "Static TLB: Host -> Device ETH L1 (MB/s)",
        "Static TLB: Device ETH L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (size_t buf_size : sizes) {
        std::vector<std::string> row;
        auto [wr_bw, rd_bw] =
            test::utils::perf_read_write(buf_size, NUM_ITERATIONS, cluster.get(), chip, eth_core, address);
        row.push_back(test::utils::convert_double_to_string(buf_size));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}
