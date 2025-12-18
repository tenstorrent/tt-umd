/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>

#include "common/microbenchmark_utils.hpp"
#include "tests/test_utils/device_test_utils.hpp"

using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

TEST(MicrobenchmarkPCIeDMA, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("DMA DRAM").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        4,
        8,
        16,
        32,
        1 * ONE_KB,
        2 * ONE_KB,
        4 * ONE_KB,
        8 * ONE_KB,
        16 * ONE_KB,
        32 * ONE_KB,
        1 * ONE_MB,
        2 * ONE_MB,
        4 * ONE_MB,
        8 * ONE_MB,
        16 * ONE_MB,
        32 * ONE_MB,
        1 * ONE_GB,
    };
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
}

TEST(MicrobenchmarkPCIeDMA, Tensix) {
    auto bench =
        ankerl::nanobench::Bench().title("DMA Tensix").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 1 * ONE_MB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
}

TEST(MicrobenchmarkPCIeDMA, Ethernet) {
    auto bench =
        ankerl::nanobench::Bench().title("DMA Ethernet").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {4, 8, 1 * ONE_KB, 2 * ONE_KB, 4 * ONE_KB, 8 * ONE_KB, 128 * ONE_KB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord eth_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::ETH)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < batch_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
}

TEST(MicrobenchmarkPCIeDMA, DRAMSweepSizes) {
    auto bench =
        ankerl::nanobench::Bench().title("DMA DRAM Sweep").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x0;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    const uint64_t LIMIT_BUF_SIZE = ONE_GB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < buf_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, dram_core, ADDRESS);
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
}

TEST(MicrobenchmarkPCIeDMA, TensixSweepSizes) {
    auto bench =
        ankerl::nanobench::Bench().title("DMA Tensix Sweep").timeUnit(std::chrono::milliseconds(1), "ms").unit("byte");
    const uint64_t ADDRESS = 0x0;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    const uint64_t LIMIT_BUF_SIZE = ONE_MB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < buf_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
}

TEST(MicrobenchmarkPCIeDMA, EthernetSweepSizes) {
    auto bench = ankerl::nanobench::Bench()
                     .title("DMA Ethernet Sweep")
                     .timeUnit(std::chrono::milliseconds(1), "ms")
                     .unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord eth_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::ETH)[0];
    const uint64_t LIMIT_BUF_SIZE = 128 * ONE_KB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        ankerl::nanobench::Rng rng;
        for (size_t i = 0; i < buf_size; ++i) {
            pattern[i] = static_cast<uint8_t>(rng());
        }
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, eth_core, ADDRESS);
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
}
