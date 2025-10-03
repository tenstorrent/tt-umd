/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/mman.h>

#include <cstddef>

#include "gtest/gtest.h"
#include "tests/test_utils/device_test_utils.hpp"

using namespace tt::umd;

constexpr chip_id_t chip = 0;
constexpr uint64_t one_mb = 1 << 20;
constexpr uint32_t NUM_ITERATIONS = 100;

static void guard_test_iommu() {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids[0]).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
}

/**
 * Measure the time it takes to map buffers of size of 1GB through IOMMU, as well as configure iATU to make these
 * buffers visible to device over NOC.
 */
TEST(MicrobenchmarkIOMMU, Map1GBPages) {
    guard_test_iommu();

    static const auto page_size = sysconf(_SC_PAGESIZE);

    std::cout << "Running IOMMU benchmark for 3 1GB pages." << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Page size: " << page_size << " bytes." << std::endl;

    const uint64_t one_gb = 1ULL << 30;  // 1 GB

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(tt::umd::ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    PCIDevice* pci_device = cluster->get_chip(chip)->get_tt_device()->get_pci_device().get();

    void* mapping0 = mmap(nullptr, one_gb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    void* mapping1 = mmap(nullptr, one_gb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    void* mapping2 = mmap(nullptr, one_gb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    uint64_t map_ns = 0;
    uint64_t unmap_ns = 0;

    for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
        {
            auto now = std::chrono::steady_clock::now();
            pci_device->map_buffer_to_noc(mapping0, one_gb);
            pci_device->map_buffer_to_noc(mapping1, one_gb);
            pci_device->map_buffer_to_noc(mapping2, one_gb);
            auto end = std::chrono::steady_clock::now();
            map_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        }

        {
            auto now = std::chrono::steady_clock::now();
            pci_device->unmap_for_dma(mapping0, one_gb);
            pci_device->unmap_for_dma(mapping1, one_gb);
            pci_device->unmap_for_dma(mapping2, one_gb);
            auto end = std::chrono::steady_clock::now();
            unmap_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        }
    }

    double avg_map_time_per_one_gb_page = (double)map_ns / (NUM_ITERATIONS * 3);
    double avg_unmap_time_per_one_gb_page = (double)unmap_ns / (NUM_ITERATIONS * 3);

    std::cout << "Average map time for 1GB page: " << avg_map_time_per_one_gb_page << " ns. ("
              << (avg_map_time_per_one_gb_page / 1e9) << " seconds)" << std::endl;
    std::cout << "Average unmap time for 1GB page: " << avg_unmap_time_per_one_gb_page << " ns. ("
              << (avg_unmap_time_per_one_gb_page / 1e9) << " seconds)" << std::endl;
}

/**
 * Measure the time it takes to map buffers of size of 1GB through IOMMU, as well as configure iATU to make these
 * buffers visible to device over NOC. This uses SysmemBuffer class to manage the buffer and its mapping, to confirm
 * there is no overhead compared to previous test.
 */
TEST(MicrobenchmarkIOMMU, Map1GBPagesSysmemBuffers) {
    guard_test_iommu();

    static const auto page_size = sysconf(_SC_PAGESIZE);

    std::cout << "Running IOMMU benchmark for 3 1GB pages." << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Page size: " << page_size << " bytes." << std::endl;

    const uint64_t one_gb = 1ULL << 30;  // 1 GB

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(tt::umd::ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    TLBManager* tlb_manager = cluster->get_chip(chip)->get_tlb_manager();

    void* mapping0 = mmap(nullptr, one_gb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    void* mapping1 = mmap(nullptr, one_gb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    void* mapping2 = mmap(nullptr, one_gb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);

    std::unique_ptr<SysmemBuffer> sysmem_buffer0 = nullptr;
    std::unique_ptr<SysmemBuffer> sysmem_buffer1 = nullptr;
    std::unique_ptr<SysmemBuffer> sysmem_buffer2 = nullptr;

    uint64_t map_ns = 0;
    uint64_t unmap_ns = 0;

    for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
        {
            auto now = std::chrono::steady_clock::now();
            sysmem_buffer0 = std::make_unique<SysmemBuffer>(tlb_manager, mapping0, one_gb, true);
            sysmem_buffer1 = std::make_unique<SysmemBuffer>(tlb_manager, mapping1, one_gb, true);
            sysmem_buffer2 = std::make_unique<SysmemBuffer>(tlb_manager, mapping2, one_gb, true);
            auto end = std::chrono::steady_clock::now();
            map_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        }

        {
            auto now = std::chrono::steady_clock::now();
            sysmem_buffer0.reset();
            sysmem_buffer1.reset();
            sysmem_buffer2.reset();
            auto end = std::chrono::steady_clock::now();
            unmap_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        }
    }

    double avg_map_time_per_one_gb_page = (double)map_ns / (NUM_ITERATIONS * 3);
    double avg_unmap_time_per_one_gb_page = (double)unmap_ns / (NUM_ITERATIONS * 3);

    std::cout << "Average map time for 1GB page: " << avg_map_time_per_one_gb_page << " ns. ("
              << (avg_map_time_per_one_gb_page / 1e9) << " seconds)" << std::endl;
    std::cout << "Average unmap time for 1GB page: " << avg_unmap_time_per_one_gb_page << " ns. ("
              << (avg_unmap_time_per_one_gb_page / 1e9) << " seconds)" << std::endl;
}
