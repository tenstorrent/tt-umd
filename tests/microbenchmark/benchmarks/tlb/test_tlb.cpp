/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "common/microbenchmark_utils.h"
#include "gtest/gtest.h"

using namespace tt::umd;

const chip_id_t chip = 0;
const uint32_t one_mb = 1 << 20;
const uint32_t NUM_ITERATIONS = 10;
const uint32_t tlb_1m_index = 0;
const uint32_t tlb_16m_index = 166;

/**
 * Measure BW of IO to DRAM core using dynamically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBDynamicDram) {
    // Sizes are chosen in a way to avoid TLB benchmark taking too long. 32 MB already
    // tests chunking of data into smaller chunks to match TLB size.
    // 64 MB and above showed the same perf locally.
    const std::vector<uint32_t> sizes = {
        1 * one_mb,
        2 * one_mb,
        4 * one_mb,
        8 * one_mb,
        16 * one_mb,
        32 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device(tt_device_params{});

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Dynamic TLB: Host -> Device DRAM (MB/s)",
        "Dynamic TLB: Device DRAM -> Host (MB/s)",
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
 * Measure BW of IO to Tensix core using dynamically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBDynamicTensix) {
    const std::vector<uint32_t> sizes = {
        1 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device(tt_device_params{});

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Dynamic TLB: Host -> Device Tensix L1 (MB/s)",
        "Dynamic TLB: Device Tensix L1 -> Host (MB/s)",
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

/**
 * Measure BW of IO to Tensix core using statically configured TLB.
 */
TEST(MicrobenchmarkTLB, TLBStaticTensix) {
    const size_t tlb_1m_index = 0;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device(tt_device_params{});

    cluster->configure_tlb(0, tensix_core, tlb_1m_index, 0x0, tlb_data::Relaxed);

    const std::vector<uint32_t> sizes = {
        1 * one_mb,
    };

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Static TLB: Host -> Device Tensix L1 (MB/s)",
        "Static TLB: Device Tensix L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (uint32_t buf_size : sizes) {
        std::vector<std::string> row;
        const uint32_t num_io = buf_size / one_mb;
        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, num_io, cluster.get(), chip, tensix_core);
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
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
    const std::vector<uint32_t> sizes = {
        16 * one_mb,
        32 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device(tt_device_params{});

    cluster->configure_tlb(0, dram_core, tlb_16m_index, 0x0, tlb_data::Relaxed);

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Static TLB: Host -> Device DRAM (MB/s)",
        "Static TLB: Device DRAM -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (uint32_t buf_size : sizes) {
        std::vector<std::string> row;
        const uint32_t num_io = buf_size / (16 * one_mb);

        auto [wr_bw, rd_bw] = test::utils::perf_read_write(buf_size, num_io, cluster.get(), chip, dram_core);
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }
    test::utils::print_markdown_table_format(headers, rows);
}
