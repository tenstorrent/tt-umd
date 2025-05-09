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

static inline void print_speed(std::string direction, size_t bytes, uint64_t ns) {
    double seconds = ns / 1e9;
    double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    auto rate = megabytes / seconds;
    std::cout << direction << ": 0x" << std::hex << bytes << std::dec << " bytes in " << ns << " ns (" << rate
              << " MB/s)" << std::endl;
}

TEST(TestPerf, TLBDynamicDram) {
    const chip_id_t chip = 0;
    const uint32_t one_mb = 1 << 20;
    const size_t NUM_ITERATIONS = 1;
    const CoreCoord dram_core = CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0);
    const std::vector<uint32_t> sizes = {
        1 * one_mb,
        2 * one_mb,
        4 * one_mb,
        8 * one_mb,
        16 * one_mb,
        32 * one_mb,
        64 * one_mb,
        128 * one_mb,
        256 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (uint32_t buf_size : sizes) {
        CoreCoord core(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::NOC0);

        std::vector<uint8_t> pattern(buf_size);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, dram_core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Dynamic TLB: Host -> Device DRAM", NUM_ITERATIONS * pattern.size(), ns);
        }

        std::vector<uint8_t> readback(buf_size, 0x0);
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            cluster->read_from_device(readback.data(), chip, dram_core, 0x0, readback.size());
        }
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        print_speed("Dynamic TLB: Device DRAM -> Host", NUM_ITERATIONS * readback.size(), ns);

        EXPECT_EQ(pattern, readback) << "Mismatch for core " << dram_core.str() << " addr=0x0"
                                     << " size=" << std::dec << readback.size();
    }
}

TEST(TestPerf, TLBDynamicTensix) {
    const chip_id_t chip = 0;
    const uint32_t one_mb = 1 << 20;
    const size_t NUM_ITERATIONS = 1;
    const CoreCoord dram_core = CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::NOC0);
    const std::vector<uint32_t> sizes = {
        1 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    cluster->start_device(tt_device_params{});

    for (uint32_t buf_size : sizes) {
        CoreCoord core(dram_core.x, dram_core.y, CoreType::DRAM, CoordSystem::NOC0);

        std::vector<uint8_t> pattern(buf_size);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < NUM_ITERATIONS; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, dram_core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Dynamic TLB: Host -> Device DRAM", NUM_ITERATIONS * pattern.size(), ns);
        }

        std::vector<uint8_t> readback(buf_size, 0x0);
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            cluster->read_from_device(readback.data(), chip, dram_core, 0x0, readback.size());
        }
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        print_speed("Dynamic TLB: Device DRAM -> Host", NUM_ITERATIONS * readback.size(), ns);

        EXPECT_EQ(pattern, readback) << "Mismatch for core " << dram_core.str() << " addr=0x0"
                                     << " size=" << std::dec << readback.size();
    }
}

TEST(TestPerf, TLBStaticTensix) {
    const chip_id_t chip = 0;
    const uint32_t one_mb = 1 << 20;
    const size_t one_mb_tlb_window_index = 0;
    CoreCoord tensix_core = CoreCoord(18, 18, CoreType::TENSIX, CoordSystem::TRANSLATED);

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    cluster->start_device(tt_device_params{});

    cluster->configure_tlb(0, tensix_core, one_mb_tlb_window_index, 0x0, TLB_DATA::Relaxed);

    const std::vector<uint32_t> sizes = {
        1 * one_mb,
    };

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mb) << " MB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        const uint32_t num_io = buf_size / one_mb;

        std::vector<uint8_t> pattern(one_mb);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());
        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, tensix_core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB: Host -> Device Tensix L1", num_io * pattern.size(), ns);
        }

        std::vector<uint8_t> readback(one_mb, 0x0);
        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->read_from_device(readback.data(), chip, tensix_core, 0x0, readback.size());
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB: Device Tensix L1 -> Host", num_io * readback.size(), ns);
        }

        EXPECT_EQ(pattern, readback);
    }
}

TEST(TestPerf, TLBStaticDram) {
    const chip_id_t chip = 0;
    const uint32_t one_mb = 1 << 20;
    const size_t tlb_window_index = 166;
    const std::vector<uint32_t> sizes = {
        16 * one_mb,
        32 * one_mb,
        64 * one_mb,
        128 * one_mb,
        256 * one_mb,
        512 * one_mb,
        1024 * one_mb,
    };
    CoreCoord dram_core = CoreCoord(0, 0, CoreType::DRAM, CoordSystem::NOC0);

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    cluster->start_device(tt_device_params{});

    cluster->configure_tlb(0, dram_core, tlb_window_index, 0x0, TLB_DATA::Relaxed);

    for (uint32_t buf_size : sizes) {
        std::cout << std::endl;
        std::cout << "Reporting results for buffer size " << (buf_size / one_mb) << " MB" << std::endl;
        std::cout << "--------------------------------------------------------" << std::endl;

        const uint32_t num_io = buf_size / (16 * one_mb);

        std::vector<uint8_t> pattern(16 * one_mb);
        test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->write_to_device(pattern.data(), pattern.size(), chip, dram_core, 0x0);
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB: Host -> Device DRAM", num_io * pattern.size(), ns);
        }

        std::vector<uint8_t> readback(16 * one_mb, 0x0);
        {
            auto now = std::chrono::steady_clock::now();
            for (int i = 0; i < num_io; i++) {
                cluster->read_from_device(readback.data(), chip, dram_core, 0x0, readback.size());
            }
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
            print_speed("Static TLB: Device DRAM -> Host", num_io * readback.size(), ns);
        }

        EXPECT_EQ(pattern, readback);
    }
}
