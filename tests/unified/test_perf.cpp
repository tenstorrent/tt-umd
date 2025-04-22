/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gtest/gtest.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/wormhole_implementation.h"
#include "wormhole/eth_l1_address_map.h"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

using namespace tt::umd;

constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

static void set_barrier_params(Cluster& cluster) {
    // Populate address map and NOC parameters that the driver needs for memory barriers and remote transactions.
    cluster.set_barrier_address_params(
        {l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, DRAM_BARRIER_BASE});
}

static inline void print_speed(std::string direction, size_t bytes, uint64_t ns) {
    double seconds = ns / 1e9;
    double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    auto rate = megabytes / seconds;
    std::cout << direction << ": 0x" << std::hex << bytes << std::dec << " bytes in " << ns << " ns (" << rate
              << " MiB/s)" << std::endl;
}

TEST(TestPerf, DynamicSmallReadWriteTLB) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t NUM_ITERATIONS = 1;
    const std::vector<tt_xy_pair> drams = {{0, 6}};
    const std::vector<uint32_t> sizes = {
        1 * one_mib,
        2 * one_mib,
        4 * one_mib,
        8 * one_mib,
        16 * one_mib,
        32 * one_mib,
        64 * one_mib,
        128 * one_mib,
        256 * one_mib,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        // Keep track of the patterns we wrote to DRAM so we can verify them later.
        std::vector<std::vector<uint8_t>> patterns;

        // First, write a different pattern to each of the DRAM cores.
        for (size_t i = 0; i < drams.size(); ++i) {
            CoreCoord core(drams[i].x, drams[i].y, CoreType::DRAM, CoordSystem::NOC0);

            std::vector<uint8_t> pattern(buf_size);
            test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("DMA: Host -> Device", NUM_ITERATIONS * pattern.size(), ns);

            patterns.push_back(pattern);
        }

        // Now, read back the patterns we wrote to DRAM and verify them.
        for (size_t i = 0; i < drams.size(); ++i) {
            CoreCoord core(drams[i].x, drams[i].y, CoreType::DRAM, CoordSystem::NOC0);

            std::vector<uint8_t> readback(buf_size, 0x0);

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->read_from_device(readback.data(), chip, core, 0x0, readback.size());
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("DMA: Device -> Host", NUM_ITERATIONS * readback.size(), ns);

            EXPECT_EQ(patterns[i], readback) << "Mismatch for core " << drams[i].str() << " addr=0x0"
                                             << " size=" << std::dec << readback.size();
        }
    }
}

TEST(TestPerf, DynamicLargeReadWriteTLB) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t NUM_ITERATIONS = 1;
    const std::vector<tt_xy_pair> drams = {{0, 6}};
    const std::vector<uint32_t> sizes = {
        1 * one_mib,
        2 * one_mib,
        4 * one_mib,
        8 * one_mib,
        16 * one_mib,
        32 * one_mib,
        64 * one_mib,
        128 * one_mib,
        256 * one_mib,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        // Keep track of the patterns we wrote to DRAM so we can verify them later.
        std::vector<std::vector<uint8_t>> patterns;

        // First, write a different pattern to each of the DRAM cores.
        for (size_t i = 0; i < drams.size(); ++i) {
            CoreCoord core(drams[i].x, drams[i].y, CoreType::DRAM, CoordSystem::NOC0);

            std::vector<uint8_t> pattern(buf_size);
            test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("DMA: Host -> Device", NUM_ITERATIONS * pattern.size(), ns);

            patterns.push_back(pattern);
        }

        // Now, read back the patterns we wrote to DRAM and verify them.
        for (size_t i = 0; i < drams.size(); ++i) {
            CoreCoord core(drams[i].x, drams[i].y, CoreType::DRAM, CoordSystem::NOC0);

            std::vector<uint8_t> readback(buf_size, 0x0);

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->read_from_device(readback.data(), chip, core, 0x0, readback.size());
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("DMA: Device -> Host", NUM_ITERATIONS * readback.size(), ns);

            EXPECT_EQ(patterns[i], readback) << "Mismatch for core " << drams[i].str() << " addr=0x0"
                                             << " size=" << std::dec << readback.size();
        }
    }
}

TEST(TestPerf, StaticReadWriteTLBTensix) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t one_mib_tlb_window_index = 0;
    CoreCoord core = CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED);

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    cluster->start_device(tt_device_params{});

    cluster->configure_tlb(0, core, one_mib_tlb_window_index, 0x0, TLB_DATA::Relaxed);

    const std::vector<uint32_t> sizes = {
        1 * one_mib,
        2 * one_mib,
        4 * one_mib,
        8 * one_mib,
        16 * one_mib,
        32 * one_mib,
        64 * one_mib,
        128 * one_mib,
        256 * one_mib,
    };

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        const uint32_t num_io = buf_size / one_mib;

        std::vector<uint8_t> pattern(one_mib);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB tensix: Host -> Device", num_io * pattern.size(), ns);
        }

        std::vector<uint8_t> readback(one_mib, 0x0);
        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->read_from_device(readback.data(), chip, core, 0x0, readback.size());
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB tensix: Device -> Host", num_io * readback.size(), ns);
        }

        EXPECT_EQ(pattern, readback);
    }
}

TEST(TestPerf, StaticReadWriteTLBDram) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t tlb_window_index = 166;
    const std::vector<uint32_t> sizes = {
        // 1 * one_mib,
        // 2 * one_mib,
        // 4 * one_mib,
        // 8 * one_mib,
        16 * one_mib,
        32 * one_mib,
        64 * one_mib,
        128 * one_mib,
        256 * one_mib,
        512 * one_mib,
        1024 * one_mib,
    };
    CoreCoord core = CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0);

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    cluster->start_device(tt_device_params{});

    cluster->configure_tlb(0, core, tlb_window_index, 0x0, TLB_DATA::Relaxed);

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        const uint32_t num_io = buf_size / (16 * one_mib);

        std::vector<uint8_t> pattern(16 * one_mib);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB tensix: Host -> Device", num_io * pattern.size(), ns);
        }

        std::vector<uint8_t> readback(16 * one_mib, 0x0);
        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->read_from_device(readback.data(), chip, core, 0x0, readback.size());
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB tensix: Device -> Host", num_io * readback.size(), ns);
        }

        EXPECT_EQ(pattern, readback);
    }
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size patterns
 * to 0x0 in several DRAM cores, then reading them back and verifying.
 */
TEST(TestPerf, DMA1) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t NUM_ITERATIONS = 5000;
    const CoreCoord dram_core = CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED);
    const std::vector<uint32_t> sizes = {
        1 * one_mib,
        // 2 * one_mib,
        // 4 * one_mib,
        // 8 * one_mib,
        // 16 * one_mib,
        // 32 * one_mib,
        // 64 * one_mib,
        // 128 * one_mib,
        // 256 * one_mib,
        // 512 * one_mib,
        // 1024 * one_mib,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        // Keep track of the patterns we wrote to DRAM so we can verify them later.
        std::vector<std::vector<uint8_t>> patterns;

        {
            std::vector<uint8_t> pattern(buf_size);
            test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->dma_write_to_device(pattern.data(), pattern.size(), chip, dram_core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("DMA: Host -> Device", NUM_ITERATIONS * pattern.size(), ns);

            patterns.push_back(pattern);
        }

        // Now, read back the patterns we wrote to DRAM and verify them.
        {
            std::vector<uint8_t> readback(buf_size, 0x0);

            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->dma_read_from_device(readback.data(), readback.size(), chip, dram_core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("DMA: Device -> Host", NUM_ITERATIONS * readback.size(), ns);

            EXPECT_EQ(patterns[0], readback) << "Mismatch for core " << dram_core.str() << " addr=0x0"
                                             << " size=" << std::dec << readback.size();
        }
    }
}
