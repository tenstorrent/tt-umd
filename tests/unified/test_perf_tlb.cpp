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
    print_speed("memcpy_total_ns", total_bytes, memcpy_total_ns);
    print_speed("dma_total_ns", total_bytes, dma_total_ns);

    double avg_memcpy_ns = (double)memcpy_total_ns / (total_bytes / std::min(dma_buf_size, total_bytes));
    std::cout << "Average memcpy time: " << avg_memcpy_ns << " ns" << std::endl;

    double memcpy_per_byte_ns = (double)memcpy_total_ns / total_bytes;
    std::cout << "memcpy time per byte: " << memcpy_per_byte_ns << " ns" << std::endl;

    double avg_dma_ns = (double)dma_total_ns / (total_bytes / std::min(dma_buf_size, total_bytes));
    std::cout << "Average DMA time: " << avg_dma_ns << " ns" << std::endl;

    double dma_per_byte_ns = (double)dma_total_ns / total_bytes;
    std::cout << "DMA time per byte: " << dma_per_byte_ns << " ns" << std::endl;

    std::cout << "Percentage of memcpy time: " << (100.0 * memcpy_total_ns / total_ns) << "%" << std::endl;

    std::cout << "Percentage of dma transaction time: " << (100.0 * dma_total_ns / total_ns) << "%" << std::endl;
    // std::cout << std::endl;
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

#include <immintrin.h>
#include <stddef.h>
#include <stdint.h>

__attribute__((target("avx"))) void simd_memcpy(void* dest, const void* src, size_t size) {
    uint8_t* dst_ptr = (uint8_t*)dest;
    const uint8_t* src_ptr = (const uint8_t*)src;

    size_t i = 0;

    // Use AVX2 256-bit registers (32 bytes at a time)
    for (; i + 31 < size; i += 32) {
        __m256i data = _mm256_loadu_si256((__m256i*)(src_ptr + i));  // unaligned load
        _mm256_storeu_si256((__m256i*)(dst_ptr + i), data);          // unaligned store
    }

    // Handle the tail (any remaining bytes)
    for (; i < size; ++i) {
        dst_ptr[i] = src_ptr[i];
    }
}

TEST(TestPerf, Memcpy) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    PCIDevice* pci_device = cluster->get_tt_device(0)->get_pci_device();

    const size_t NUM_ITERATIONS = 1000;

    const uint32_t one_mib = (1 << 20);

    {
        std::vector<uint8_t> src_buffer(one_mib);
        test_utils::fill_with_random_bytes(&src_buffer[0], src_buffer.size());
        auto now = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            memcpy(pci_device->get_dma_buffer().buffer, src_buffer.data(), src_buffer.size());
        }

        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

        print_speed(
            "Single thread memcpy - each memcpy 1MB: Host -> DMA buffer", NUM_ITERATIONS * src_buffer.size(), ns);
    }
    {
        std::vector<uint8_t> src_buffer(one_mib / 2);
        auto now = std::chrono::steady_clock::now();

        for (int i = 0; i < 2 * NUM_ITERATIONS; i++) {
            memcpy(pci_device->get_dma_buffer().buffer, src_buffer.data(), src_buffer.size());
        }

        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

        print_speed(
            "Single thread memcpy - each memcpy 512KB: Host -> DMA buffer", 2 * NUM_ITERATIONS * src_buffer.size(), ns);
    }
    {
        std::vector<uint8_t> src_buffer(one_mib);
        test_utils::fill_with_random_bytes(&src_buffer[0], src_buffer.size());
        auto now = std::chrono::steady_clock::now();

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            simd_memcpy(pci_device->get_dma_buffer().buffer, src_buffer.data(), src_buffer.size());
        }

        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

        print_speed("SIMD memcpy - each memcpy 1MB: Host -> DMA buffer", NUM_ITERATIONS * src_buffer.size(), ns);
    }
    {
        std::vector<uint8_t> src_buffer(one_mib / 4);
        auto now = std::chrono::steady_clock::now();

        for (int i = 0; i < 4 * NUM_ITERATIONS; i++) {
            memcpy(pci_device->get_dma_buffer().buffer, src_buffer.data(), src_buffer.size());
        }

        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

        print_speed(
            "Single thread memcpy - each memcpy 256KB: Host -> DMA buffer", 4 * NUM_ITERATIONS * src_buffer.size(), ns);
    }
    {
        std::vector<uint8_t> src_buffer(one_mib);
        test_utils::fill_with_random_bytes(&src_buffer[0], src_buffer.size());
        auto now = std::chrono::steady_clock::now();

        void* s1 = (void*)((uint8_t*)src_buffer.data() + src_buffer.size() / 4);
        void* s2 = (void*)((uint8_t*)src_buffer.data() + src_buffer.size() / 2);
        void* s3 = (void*)((uint8_t*)src_buffer.data() + 3 * src_buffer.size() / 4);

        void* p1 = (void*)((uint8_t*)pci_device->get_dma_buffer().buffer + src_buffer.size() / 4);
        void* p2 = (void*)((uint8_t*)pci_device->get_dma_buffer().buffer + src_buffer.size() / 2);
        void* p3 = (void*)((uint8_t*)pci_device->get_dma_buffer().buffer + 3 * src_buffer.size() / 4);

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            auto memcpy0 = std::thread(
                [&] { memcpy(pci_device->get_dma_buffer().buffer, src_buffer.data(), src_buffer.size() / 4); });

            auto memcpy1 = std::thread([&] { memcpy(p1, s1, src_buffer.size() / 4); });

            auto memcpy2 = std::thread([&] { memcpy(p2, s2, src_buffer.size() / 4); });

            auto memcpy3 = std::thread([&] { memcpy(p3, s3, src_buffer.size() / 4); });

            memcpy0.join();
            memcpy1.join();
            memcpy2.join();
            memcpy3.join();
        }

        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        print_speed("Multiple threads memcpy: Host -> DMA buffer", NUM_ITERATIONS * src_buffer.size(), ns);
    }
}
