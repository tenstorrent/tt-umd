// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

TEST(MicrobenchmarkEthernetIO, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("EthernetIO_DRAM").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 1 * ONE_MB, 2 * ONE_MB, 4 * ONE_MB, 8 * ONE_MB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const ChipId chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device({});
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(batch_size).name(fmt::format("ETH IO - DRAM, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), chip, dram_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("ETH IO - DRAM, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), chip, dram_core, ADDRESS, batch_size);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkEthernetIO, Tensix) {
    auto bench = ankerl::nanobench::Bench().title("EthernetIO_Tensix").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 1 * ONE_MB, 2 * ONE_MB, 4 * ONE_MB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const ChipId chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device({});
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(batch_size).name(fmt::format("ETH IO - Tensix, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), chip, tensix_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("ETH IO - Tensix, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), chip, tensix_core, ADDRESS, batch_size);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkEthernetIO, Ethernet) {
    auto bench = ankerl::nanobench::Bench()
                     .title("EthernetIO_Ethernet")

                     .unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {1, 2, 4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 128 * ONE_KB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const ChipId chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord eth_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::ETH)[0];
    cluster->start_device({});
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(batch_size).name(fmt::format("ETH IO - Ethernet, write, {} bytes", batch_size)).run([&]() {
            cluster->write_to_device(pattern.data(), pattern.size(), chip, eth_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("ETH IO - Ethernet, read, {} bytes", batch_size)).run([&]() {
            cluster->read_from_device(pattern.data(), chip, eth_core, ADDRESS, batch_size);
        });
    }
    test::utils::export_results(bench);
}
