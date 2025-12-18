/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

TEST(MicrobenchmarkTLB, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("TLB DRAM").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1,
        2,
        4,
        8,
        1 * ONE_KB,
        2 * ONE_KB,
        4 * ONE_KB,
        8 * ONE_KB,
        1 * ONE_MB,
        2 * ONE_MB,
        4 * ONE_MB,
        8 * ONE_MB,
        16 * ONE_MB,
        32 * ONE_MB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        GTEST_SKIP() << "No chips found on system.";
    }
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
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
    cluster->configure_tlb(CHIP_ID, dram_core, 16 * (1 << 20), ADDRESS, tlb_data::Relaxed);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
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
}

TEST(MicrobenchmarkTLB, Tensix) {
    auto bench =
        ankerl::nanobench::Bench().title("TLB Tensix").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {1, 2, 4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 1 * ONE_MB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_number_of_chips() == 0) {
        GTEST_SKIP() << "No chips found on system.";
    }
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
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
    cluster->configure_tlb(CHIP_ID, tensix_core, 1 << 21, ADDRESS, tlb_data::Relaxed);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
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
}

TEST(MicrobenchmarkTLB, Ethernet) {
    auto bench =
        ankerl::nanobench::Bench().title("TLB Ethernet").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {1, 2, 4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 128 * ONE_KB};
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
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
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
    cluster->configure_tlb(CHIP_ID, eth_core, 1 << 21, ADDRESS, tlb_data::Relaxed);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
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
}
