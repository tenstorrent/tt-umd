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

// Measure BW of IO to DRAM core on the ETH connected device.
TEST(MicrobenchmarkEthernetIO, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("EthernetIO_DRAM").unit("byte");
    const uint64_t ADDRESS = 0x0;
    // Sizes are chosen in a way to avoid TLB benchmark taking too long. 32 MB already
    // tests chunking of data into smaller chunks to match TLB size.
    // 64 MB and above showed the same perf locally.
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
        8 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const ChipId chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device({});
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
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

// Measure BW of IO to Tensix core on ETH connected device.
TEST(MicrobenchmarkEthernetIO, Tensix) {
    auto bench = ankerl::nanobench::Bench().title("EthernetIO_Tensix").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const ChipId chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device({});
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
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

// Measure BW of IO to Ethernet core on ETH connected device.
TEST(MicrobenchmarkEthernetIO, Ethernet) {
    auto bench = ankerl::nanobench::Bench().title("EthernetIO_Ethernet").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 128 * ONE_KIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_remote_device_ids().empty()) {
        GTEST_SKIP() << "No ETH connected devices found in the cluster, skipping benchmark.";
    }

    const ChipId chip = *cluster->get_target_remote_device_ids().begin();
    const CoreCoord eth_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::ETH)[0];
    cluster->start_device({});
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
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
