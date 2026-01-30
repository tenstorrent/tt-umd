// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

// Measure bandwidth of IO to DRAM core.
TEST(MicrobenchmarkTLB, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("TLB_DRAM").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1,
        2,
        4,
        8,
        1 * ONE_KIB,
        2 * ONE_KIB,
        4 * ONE_KIB,
        8 * ONE_KIB,
        1 * ONE_MIB,
        2 * ONE_MIB,
        4 * ONE_MIB,
        8 * ONE_MIB,
        16 * ONE_MIB,
        32 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        GTEST_SKIP() << "No chips found on system.";
    }
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Dynamic TLB, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Dynamic TLB, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), CHIP_ID, dram_core, ADDRESS, batch_size);
        });
    }
    // Static TLB configuration.
    cluster->configure_tlb(CHIP_ID, dram_core, 2 * ONE_MIB, ADDRESS, tlb_data::Relaxed);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Static TLB, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Static TLB, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), CHIP_ID, dram_core, ADDRESS, batch_size);
        });
    }
    test::utils::export_results(bench);
}

// Measure bandwidth of IO to Tensix core.
TEST(MicrobenchmarkTLB, Tensix) {
    auto bench = ankerl::nanobench::Bench().title("TLB_Tensix").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        GTEST_SKIP() << "No chips found on system.";
    }
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Dynamic TLB, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Dynamic TLB, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), CHIP_ID, tensix_core, ADDRESS, batch_size);
        });
    }
    // Static TLB configuration.
    cluster->configure_tlb(CHIP_ID, tensix_core, 2 * ONE_MIB, ADDRESS, tlb_data::Relaxed);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Static TLB, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Static TLB, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), CHIP_ID, tensix_core, ADDRESS, batch_size);
        });
    }
    test::utils::export_results(bench);
}

// Measure bandwidth of IO to Ethernet core.
TEST(MicrobenchmarkTLB, Ethernet) {
    auto bench = ankerl::nanobench::Bench().title("TLB_Ethernet").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 128 * ONE_KIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        GTEST_SKIP() << "No chips found on system.";
    }
    if (cluster->get_soc_descriptor(CHIP_ID).get_num_eth_channels() == 0) {
        GTEST_SKIP() << "No ETH cores found on system.";
    }
    const CoreCoord eth_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::ETH).at(0);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Dynamic TLB, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Dynamic TLB, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), CHIP_ID, eth_core, ADDRESS, batch_size);
        });
    }
    // Static TLB configuration.
    cluster->configure_tlb(CHIP_ID, eth_core, 2 * ONE_MIB, ADDRESS, tlb_data::Relaxed);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Static TLB, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("Static TLB, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), CHIP_ID, eth_core, ADDRESS, batch_size);
        });
    }
    test::utils::export_results(bench);
}

// Since multicast has multiple endpoints as targets, it's not compeletely fair to compare
// the bandwidth, which is still tied to TLB bandwidth. BW of multicast writes will be the same in terms
// of BW as unicast writes. The benefit of multicast is in saving time by writing to multiple endpoints in one go.
// However, it is interesting to see the time taken for unicast vs multicast writes to multiple endpoints.
// That is why this test is disabled by default. It's meant for someone to run it manually if needed.
TEST(MicrobenchmarkTLB, CompareMulticastandUnicast) {
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1,
        2,
        4,
        8,
        1 * ONE_KIB,
        2 * ONE_KIB,
        4 * ONE_KIB,
        8 * ONE_KIB,
        16 * ONE_KIB,
        32 * ONE_KIB,
        64 * ONE_KIB,
        128 * ONE_KIB,
        256 * ONE_KIB,
        512 * ONE_KIB,
        1 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        GTEST_SKIP() << "No chips found on system.";
    }
    std::vector<Result> results;
    auto tensix_cores = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX);
    for (size_t batch_size : BATCH_SIZES) {
        auto bench = ankerl::nanobench::Bench().title("TLB_Tensix_Unicast_v_Multicast").unit("byte");
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size)
            .name(fmt::format("Unicast, {} cores, {} bytes", tensix_cores.size(), batch_size))
            .relative(true)
            .run([&]() {
                for (auto &tensix_core : tensix_cores) {
                    cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
                }
            });
        bench.batch(batch_size)
            .name(fmt::format("Multicast, {} cores, {} bytes", tensix_cores.size(), batch_size))
            .run([&]() {
                cluster->noc_multicast_write(
                    pattern.data(), pattern.size(), CHIP_ID, tensix_cores.front(), tensix_cores.back(), ADDRESS);
            });

        results.reserve(results.size() + bench.results().size());
        results.insert(results.end(), bench.results().begin(), bench.results().end());
    }
    export_results("TLB_Tensix_Unicast_v_Multicast", results);
}
