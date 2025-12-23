// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

/**
 * Measure the time it takes to map buffers of different sizes through IOMMU.
 * This test allocates buffers of different size, starting from single page (usually 4KB) up to 1GB,
 * and measures the time it takes to map them through IOMMU. It prints out the time taken for each mapping
 * and the average time per page.
 */
TEST(MicrobenchmarkIOMMU, MapDifferentSizes) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }

    auto bench = ankerl::nanobench::Bench()
                     .title("IOMMU_MapDifferentSizes")

                     .unit("byte");

    static const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t MAPPING_SIZE_LIMIT = ONE_GB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device().get();
    for (uint64_t size = page_size; size <= MAPPING_SIZE_LIMIT; size *= 2) {
        void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        bench.batch(size).name(fmt::format("Map + Unmap, {} bytes", size)).run([&]() {
            uint64_t iova = pci_device->map_for_dma(mapping, size);
            ankerl::nanobench::doNotOptimizeAway(iova);
            pci_device->unmap_for_dma(mapping, size);
        });
        munmap(mapping, size);
    }

    test::utils::export_results(bench);
}

/**
 * Measure the time it takes to map hugepages using IOMMU.
 * These should be different from regular buffers because it's guaranteed that hugepages are contiguous in memory.
 * Since contiguous memory has less entries in the IOMMU page table, we expect the mapping to be faster.
 */
TEST(MicrobenchmarkIOMMU, MapHugepages2M) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }

    auto bench = ankerl::nanobench::Bench().title("IOMMU_HugePage2M").minEpochIterations(2000);

    static const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t HUGEPAGE_SHIFT = 21;
    const uint64_t MAPPING_SIZE = 1 << HUGEPAGE_SHIFT;  // 2 MiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device().get();

    void* mapping = mmap(
        0,
        MAPPING_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (HUGEPAGE_SHIFT << MAP_HUGE_SHIFT),
        -1,
        0);

    if (mapping == MAP_FAILED) {
        GTEST_SKIP() << "Mapping 2MB hugepage failed. Skipping test.";
    }

    bench.name(fmt::format("Map + Unmap, {} bytes", MAPPING_SIZE)).run([&]() {
        uint64_t iova = pci_device->map_for_dma(mapping, MAPPING_SIZE);
        ankerl::nanobench::doNotOptimizeAway(iova);
        pci_device->unmap_for_dma(mapping, MAPPING_SIZE);
    });
    munmap(mapping, MAPPING_SIZE);

    test::utils::export_results(bench);
}

/**
 * Measure the time it takes to map hugepages using IOMMU.
 * These should be different from regular buffers because it's guaranteed that hugepages are contiguous in memory.
 * Since contiguous memory has less entries in the IOMMU page table, we expect the mapping to be faster.
 */
TEST(MicrobenchmarkIOMMU, MapHugepages1G) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }

    auto bench = ankerl::nanobench::Bench().title("IOMMU_HugePage1G");

    static const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t HUGEPAGE_SHIFT = 30;
    const uint64_t MAPPING_SIZE = 1 << HUGEPAGE_SHIFT;  // 1 GiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device().get();

    void* mapping = mmap(
        0,
        MAPPING_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (HUGEPAGE_SHIFT << MAP_HUGE_SHIFT),
        -1,
        0);

    if (mapping == MAP_FAILED) {
        GTEST_SKIP() << "Mapping 1GB hugepage failed. Skipping test.";
    }

    bench.name(fmt::format("Map + Unmap, {} bytes", MAPPING_SIZE)).run([&]() {
        uint64_t iova = pci_device->map_for_dma(mapping, MAPPING_SIZE);
        ankerl::nanobench::doNotOptimizeAway(iova);
        pci_device->unmap_for_dma(mapping, MAPPING_SIZE);
    });
    munmap(mapping, MAPPING_SIZE);

    test::utils::export_results(bench);
}
