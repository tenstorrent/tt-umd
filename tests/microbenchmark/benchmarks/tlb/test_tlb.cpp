// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "common/microbenchmark_utils.hpp"

using namespace tt;
using namespace tt::umd;

constexpr ChipId chip = 0;
constexpr size_t one_kb = 1 << 10;
constexpr size_t one_mb = 1 << 20;
constexpr uint32_t NUM_ITERATIONS = 10;

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

    cluster->configure_tlb(0, tensix_core, 1 << 21, 0, tlb_data::Relaxed);

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

    cluster->configure_tlb(0, dram_core, 16 * (1 << 20), 0, tlb_data::Relaxed);

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
    cluster->configure_tlb(chip, eth_core, 1 << 21, address, tlb_data::Relaxed);

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

// Since multicast has multiple endpoints as targets, it's not compeletely fair to compare
// the bandwidth, which is still tied to TLB bandwidth. BW of multicast writes will be the same in terms
// of BW as unicast writes. The benefit of multicast is in saving time by writing to multiple endpoints in one go.
// However, it is interesting to see the time taken for unicast vs multicast writes to multiple endpoints.
// That is why this test is disabled by default. It's meant for someone to run it manually if needed.
TEST(MicrobenchmarkTLB, CompareMulticastandUnicast) {
    const std::vector<size_t> sizes = {
        1,
        2,
        4,
        8,
        1 * one_kb,
        2 * one_kb,
        4 * one_kb,
        8 * one_kb,
        16 * one_kb,
        32 * one_kb,
        64 * one_kb,
        128 * one_kb,
        256 * one_kb,
        512 * one_kb,
        1 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    auto tensix_cores = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX);

    for (size_t buf_size : sizes) {
        std::cout << "Comparing multicast and unicast for size: " << buf_size << " bytes." << std::endl;

        std::vector<uint8_t> buffer(buf_size, 0);

        double result_ns_unicast = 0;
        double result_ns_multicast = 0;

        {
            double total_ns = 0;
            for (auto &tensix_core : tensix_cores) {
                auto start = std::chrono::steady_clock::now();
                cluster->write_to_device(buffer.data(), buf_size, chip, tensix_core, 0);
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                total_ns += ns;
            }
            result_ns_unicast = total_ns;
            std::cout << "Unicast write time to all tensix cores: " << result_ns_unicast / (1e9) << " s." << std::endl;
        }

        {
            double total_ns = 0;
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                auto start = std::chrono::steady_clock::now();
                cluster->noc_multicast_write(
                    buffer.data(), buf_size, chip, tensix_cores[0], tensix_cores[tensix_cores.size() - 1], 0);
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                total_ns += ns;
            }
            result_ns_multicast = total_ns / NUM_ITERATIONS;
            std::cout << "Multicast write time to all tensix cores: " << result_ns_multicast / (1e9) << " s."
                      << std::endl;
        }

        std::cout << "Speedup (Unicast / Multicast): "
                  << static_cast<double>(result_ns_unicast) / static_cast<double>(result_ns_multicast) << "x"
                  << std::endl;
    }
}
