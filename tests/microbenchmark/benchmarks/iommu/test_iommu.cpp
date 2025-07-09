/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/mman.h>

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

const chip_id_t chip = 0;
const uint64_t one_mb = 1 << 20;

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
 * Measure the time it takes to map buffers of different sizes through IOMMU.
 * This test allocates buffers of different size, starting from single page (usually 4KB) up to 1GB,
 * and measures the time it takes to map them through IOMMU. It prints out the time taken for each mapping
 * and the average time per page.
 */
TEST(BenchmarkIOMMU, MapDifferentSizes) {
    guard_test_iommu();

    static const auto page_size = sysconf(_SC_PAGESIZE);

    const std::vector<uint64_t> mapping_sizes;

    std::cout << "Running IOMMU benchmark for different mapping sizes." << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Page size: " << page_size << " bytes." << std::endl;

    const uint64_t mapping_size_limit = 1ULL << 30;  // 1 GB

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(tt::umd::ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    SysmemManager* sysmem_manager = cluster->get_chip(chip)->get_sysmem_manager();

    for (uint64_t size = page_size; size <= mapping_size_limit; size *= 2) {
        void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        if (mapping == MAP_FAILED) {
            std::cerr << "Failed to allocate mapping of size " << size << " bytes." << std::endl;
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping, size);
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();

        const uint64_t num_pages = size / page_size;
        double map_per_page = ns / num_pages;

        std::cout << "Mapped " << num_pages << " pages (" << size << " bytes, " << ((double)size / one_mb) << " MB) in "
                  << ns << " ns. Mapping time per page: " << map_per_page << " ns." << std::endl;

        munmap(mapping, size);
    }
}
