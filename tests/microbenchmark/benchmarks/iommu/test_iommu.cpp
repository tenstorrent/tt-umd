// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;
constexpr uint32_t NUM_EPOCHS = 100;

// Measure the time it takes to map buffers of different sizes through IOMMU.
// This test allocates buffers of different size, starting from single page (usually 4KiB) up to 1GiB,
// and measures the time it takes to map them through IOMMU. It prints out the time taken for each mapping
// and the average time per page.
TEST(MicrobenchmarkIOMMU, MapDifferentSizes) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_MapDifferentSizes").unit("byte").epochs(NUM_EPOCHS).epochIterations(1);
    std::vector<Result> results;
    ankerl::nanobench::detail::PerformanceCounters const pc;  // Empty perf. counters just to fill in args.

    static const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t MAPPING_SIZE_LIMIT = ONE_GIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device().get();
    for (uint64_t size = page_size; size <= MAPPING_SIZE_LIMIT; size *= 2) {
        bench.name(fmt::format("Map {} bytes", size)).batch(size);
        ankerl::nanobench::Result map_result(bench.config());
        bench.name(fmt::format("Unmap {} bytes", size));
        ankerl::nanobench::Result unmap_result(bench.config());
        for (int i = 0; i < NUM_EPOCHS; i++) {
            void* mapping =
                mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
            auto now = std::chrono::high_resolution_clock::now();
            pci_device->map_for_dma(mapping, size);
            auto end = std::chrono::high_resolution_clock::now();
            map_result.add(end - now, 1, pc);

            now = std::chrono::high_resolution_clock::now();
            pci_device->unmap_for_dma(mapping, size);
            end = std::chrono::high_resolution_clock::now();
            unmap_result.add(end - now, 1, pc);
            munmap(mapping, size);
        }
        results.push_back(std::move(map_result));
        results.push_back(std::move(unmap_result));
    }

    test::utils::export_results(bench.title(), results);
}

// Measure the time it takes to map hugepages using IOMMU.
// These should be different from regular buffers because it's guaranteed that hugepages are contiguous in memory.
// Since contiguous memory has less entries in the IOMMU page table, we expect the mapping to be faster.
TEST(MicrobenchmarkIOMMU, MapHugepages2M) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    const uint64_t HUGEPAGE_SHIFT = 21;
    const uint64_t MAPPING_SIZE = 1 << HUGEPAGE_SHIFT;  // 2 MiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device().get();

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_HugePage2M").epochIterations(1).epochs(NUM_EPOCHS).name("Map 2M");
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 2M");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters const pc;  // Empty perf. counters just to fill in args.
    for (int i = 0; i < NUM_EPOCHS; i++) {
        void* mapping = mmap(
            nullptr,
            MAPPING_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (HUGEPAGE_SHIFT << MAP_HUGE_SHIFT),
            -1,
            0);

        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 2MiB hugepage failed. Skipping test.";
        }
        auto now = std::chrono::high_resolution_clock::now();
        pci_device->map_for_dma(mapping, MAPPING_SIZE);
        auto end = std::chrono::high_resolution_clock::now();
        map_result.add(end - now, 1, pc);

        now = std::chrono::high_resolution_clock::now();
        pci_device->unmap_for_dma(mapping, MAPPING_SIZE);
        end = std::chrono::high_resolution_clock::now();
        unmap_result.add(end - now, 1, pc);

        munmap(mapping, MAPPING_SIZE);
    }

    test::utils::export_results(bench.title(), std::vector<ankerl::nanobench::Result>{map_result, unmap_result});
}

// Measure the time it takes to map hugepages using IOMMU.
// These should be different from regular buffers because it's guaranteed that hugepages are contiguous in memory.
// Since contiguous memory has less entries in the IOMMU page table, we expect the mapping to be faster.
TEST(MicrobenchmarkIOMMU, MapHugepages1G) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    const uint64_t HUGEPAGE_SHIFT = 30;
    const uint64_t MAPPING_SIZE = 1 << HUGEPAGE_SHIFT;  // 1 GiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device().get();

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_HugePage1G").epochIterations(1).epochs(NUM_EPOCHS).name("Map 1G");
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 1G");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters const pc;  // Empty perf. counters just to fill in args.
    for (int i = 0; i < NUM_EPOCHS; i++) {
        void* mapping = mmap(
            nullptr,
            MAPPING_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (HUGEPAGE_SHIFT << MAP_HUGE_SHIFT),
            -1,
            0);

        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 1GiB hugepage failed. Skipping test.";
        }
        auto now = std::chrono::high_resolution_clock::now();
        pci_device->map_for_dma(mapping, MAPPING_SIZE);
        auto end = std::chrono::high_resolution_clock::now();
        map_result.add(end - now, 1, pc);

        now = std::chrono::high_resolution_clock::now();
        pci_device->unmap_for_dma(mapping, MAPPING_SIZE);
        end = std::chrono::high_resolution_clock::now();
        unmap_result.add(end - now, 1, pc);

        munmap(mapping, MAPPING_SIZE);
    }

    test::utils::export_results(bench.title(), std::vector<ankerl::nanobench::Result>{map_result, unmap_result});
}
