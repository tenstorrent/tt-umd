/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <thread>

#include "gtest/gtest.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/chip_helpers/sysmem_manager.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_device/wormhole_tt_device.h"
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
              << " MB/s)" << std::endl;
}

static inline void print_stats(
    uint64_t dma_buf_size,
    std::string direction,
    uint64_t total_bytes,
    uint64_t total_ns,
    uint64_t memcpy_total_ns,
    uint64_t dma_total_ns) {
    std::cout << std::endl;
    static const uint32_t one_kb = 1 << 10;
    std::cout << "Reporting results for direction " << direction << " and transfering 0x" << std::hex << total_bytes
              << std::dec << " bytes and DMA buffer size " << (dma_buf_size / one_kb) << " KB" << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    print_speed(direction, total_bytes, total_ns);
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size pattern
 * to 0x0 tensix core, then reading them back and verifying.
 */
TEST(TestPerf, DMATensix) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t NUM_ITERATIONS = 5000;
    const CoreCoord core = CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED);
    const std::vector<uint32_t> sizes = {
        1 * one_mib,
    };

    const std::vector<uint32_t> dma_buf_sizes = {1 << 18, 1 << 19, 1 << 20, 1 << 21};

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (size_t dma_buf_size_ : dma_buf_sizes) {
        PCIDevice::dma_buf_size = dma_buf_size_;

        const uint32_t dma_buf_size = cluster->get_tt_device(0)->get_pci_device()->get_dma_buffer().size;

        for (uint32_t buf_size : sizes) {
            // std::cout << std::endl;
            // std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
            // std::cout << "--------------------------------------------------------" << std::endl;

            // Keep track of the patterns we wrote to tensix so we can verify them later.
            std::vector<std::vector<uint8_t>> patterns;
            {
                std::vector<uint8_t> pattern(buf_size);
                test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

                auto now = std::chrono::steady_clock::now();
                for (int i = 0; i < NUM_ITERATIONS; i++) {
                    cluster->dma_write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
                }
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
                print_stats(dma_buf_size, "DMA: Host -> Device", NUM_ITERATIONS * pattern.size(), ns, 0, 0);

                patterns.push_back(pattern);
            }

            std::cout << std::endl;

            // Now, read back the patterns we wrote to tensix and verify them.
            {
                std::vector<uint8_t> readback(buf_size, 0x0);

                auto now = std::chrono::steady_clock::now();
                for (int i = 0; i < NUM_ITERATIONS; i++) {
                    cluster->dma_read_from_device(readback.data(), readback.size(), chip, core, 0x0);
                }
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
                print_stats(dma_buf_size, "DMA: Device -> Host", NUM_ITERATIONS * readback.size(), ns, 0, 0);

                EXPECT_EQ(patterns[0], readback) << "Mismatch for core " << core.str() << " addr=0x0"
                                                 << " size=" << std::dec << readback.size();
            }
        }
    }
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size pattern
 * to 0x0 tensix core, then reading them back and verifying.
 */
TEST(TestPerf, DMADram) {
    const chip_id_t chip = 0;
    const uint32_t one_mib = 1 << 20;
    const size_t NUM_ITERATIONS = 1000;
    const CoreCoord core = CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0);
    const std::vector<uint32_t> sizes = {
        // 1 * one_mib,
        // 2 * one_mib,
        4 * one_mib,
        // 8 * one_mib,
        // 16 * one_mib,
        // 32 * one_mib,
        // 64 * one_mib,
        // 128 * one_mib,
        // 256 * one_mib,
        // 512 * one_mib,
        // 1024 * one_mib,
    };

    const std::vector<uint32_t> dma_buf_sizes = {1 << 18, 1 << 19, 1 << 20, 1 << 21};

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (size_t dma_buf_size_ : dma_buf_sizes) {
        PCIDevice::dma_buf_size = dma_buf_size_;

        const uint32_t dma_buf_size = cluster->get_tt_device(0)->get_pci_device()->get_dma_buffer().size;

        for (uint32_t buf_size : sizes) {
            std::cout << std::endl;
            std::cout << "Reporting results for buffer size " << (buf_size / one_mib) << " MiB" << std::endl;
            std::cout << "--------------------------------------------------------" << std::endl;

            // Keep track of the patterns we wrote to tensix so we can verify them later.
            std::vector<std::vector<uint8_t>> patterns;
            {
                std::vector<uint8_t> pattern(buf_size);
                test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

                auto now = std::chrono::steady_clock::now();
                for (int i = 0; i < NUM_ITERATIONS; i++) {
                    cluster->dma_write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
                }
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
                print_stats(dma_buf_size, "DMA: Host -> Device", NUM_ITERATIONS * pattern.size(), ns, 0, 0);

                patterns.push_back(pattern);
            }

            std::cout << std::endl;

            // Now, read back the patterns we wrote to tensix and verify them.
            {
                std::vector<uint8_t> readback(buf_size, 0x0);

                auto now = std::chrono::steady_clock::now();
                for (int i = 0; i < NUM_ITERATIONS; i++) {
                    cluster->dma_read_from_device(readback.data(), readback.size(), chip, core, 0x0);
                }
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
                print_stats(dma_buf_size, "DMA: Device -> Host", NUM_ITERATIONS * readback.size(), ns, 0, 0);

                EXPECT_EQ(patterns[0], readback) << "Mismatch for core " << core.str() << " addr=0x0"
                                                 << " size=" << std::dec << readback.size();
            }
        }
    }
}

/**
 * Test the PCIe DMA controller by using it to write random tile-sized patterns
 * to DRAM banks, in a same way Metal would write data for DRAM interleaved pattern.
 */
TEST(TestPerf, DMADramInterleaved) {
    const chip_id_t chip = 0;
    const uint64_t one_kb = 1 << 10;
    const uint64_t tile_size = 32 * 32;
    const std::vector<CoreCoord> dram_cores = {
        CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(0, 5, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(0, 5, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 0, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 0, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 2, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 2, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 3, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 3, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 5, CoreType::DRAM, CoordSystem::NOC0),
        CoreCoord(5, 5, CoreType::DRAM, CoordSystem::NOC0),
    };
    const std::vector<uint64_t> dram_addrs = {
        0,
        1ULL << 30,
        0,
        1ULL << 30,
        0,
        1ULL << 30,
        0,
        1ULL << 30,
        0,
        1ULL << 30,
        0,
        1ULL << 30,
    };
    const std::vector<uint64_t> tensor_sizes = {
        1 * 1 * tile_size,
        2 * 2 * tile_size,
        4 * 4 * tile_size,
        8 * 8 * tile_size,
        16 * 16 * tile_size,
        32 * 32 * tile_size,
        64 * 64 * tile_size,
        128 * 128 * tile_size,
        256 * 256 * tile_size,
        512 * 512 * tile_size,
        1024 * 1024 * tile_size,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (uint32_t buf_size : tensor_sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_kb) << " KB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        std::vector<uint64_t> dram_addrs_io = dram_addrs;
        uint32_t dram_core_index = 0;
        uint32_t dram_addr_index = 0;

        uint64_t total_ns = 0;

        // Keep track of the patterns we wrote to DRAM so we can verify them later.
        std::vector<std::vector<uint8_t>> patterns;
        {
            for (size_t i = 0; i < buf_size / tile_size; i++) {
                std::vector<uint8_t> pattern(tile_size);
                test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

                auto now = std::chrono::steady_clock::now();
                cluster->dma_write_to_device(
                    pattern.data(), pattern.size(), chip, dram_cores[dram_core_index], dram_addrs_io[dram_addr_index]);
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

                total_ns += ns;

                dram_addrs_io[dram_addr_index] += tile_size;

                patterns.push_back(pattern);

                dram_core_index = (dram_core_index + 1) % dram_cores.size();
                dram_addr_index = (dram_addr_index + 1) % dram_addrs.size();
            }

            print_speed("DMA: Host -> Device", buf_size, total_ns);
        }

        dram_addrs_io = dram_addrs;
        dram_core_index = 0;
        dram_addr_index = 0;
        total_ns = 0;

        {
            // Now, read back the patterns we wrote to DRAM and verify them.
            for (size_t i = 0; i < buf_size / tile_size; i++) {
                std::vector<uint8_t> readback(tile_size, 0x0);

                auto now = std::chrono::steady_clock::now();
                cluster->dma_read_from_device(
                    readback.data(),
                    readback.size(),
                    chip,
                    dram_cores[dram_core_index],
                    dram_addrs_io[dram_addr_index]);
                auto end = std::chrono::steady_clock::now();
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

                total_ns += ns;

                dram_addrs_io[dram_addr_index] += tile_size;

                EXPECT_EQ(patterns[i], readback)
                    << "Mismatch for core " << dram_cores[dram_core_index].str() << " addr=0x0"
                    << " size=" << std::dec << readback.size();

                dram_core_index = (dram_core_index + 1) % dram_cores.size();
                dram_addr_index = (dram_addr_index + 1) % dram_addrs.size();
            }

            print_speed("DMA: Device -> Host", buf_size, total_ns);
        }
    }
}
