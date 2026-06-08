// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/chip/chip.hpp"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

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
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
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
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.

    static const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t MAPPING_SIZE_LIMIT = ONE_GIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device();
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
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
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
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device();

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_HugePage2M").epochIterations(1).epochs(NUM_EPOCHS).name("Map 2M");
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 2M");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.
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

// Measure the time it takes to map 512MiB hugepages using IOMMU.
// 512MiB hugepages are PMD/level-2 block descriptors on AArch64 with 64K base pages.
// They are not available on x86. Each hugepage maps to one SMMU TLB entry regardless of
// size, so they yield fewer TLB entries than 2MiB pages for the same total memory.
TEST(MicrobenchmarkIOMMU, MapHugepages512M) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    const uint64_t HUGEPAGE_SHIFT = 29;
    const uint64_t MAPPING_SIZE = 512ULL << 20;  // 512 MiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device();

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_HugePage512M").epochIterations(1).epochs(NUM_EPOCHS).name("Map 512M");
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 512M");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.
    for (int i = 0; i < NUM_EPOCHS; i++) {
        void* mapping = mmap(
            nullptr,
            MAPPING_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (HUGEPAGE_SHIFT << MAP_HUGE_SHIFT),
            -1,
            0);

        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 512MiB hugepage failed. Skipping test.";
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
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
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
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device();

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_HugePage1G").epochIterations(1).epochs(NUM_EPOCHS).name("Map 1G");
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 1G");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.
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

// Measure the time it takes to map buffers of different sizes through IOMMU, as well as configure iATU to make these
// buffers visible to the device over NOC. This is the equivalent of MapDifferentSizes, except it goes through
// map_buffer_to_noc (IOMMU mapping + iATU configuration) instead of map_for_dma (IOMMU mapping only).
TEST(MicrobenchmarkIOMMU, MapDifferentSizesBufferToNOC) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    auto bench = ankerl::nanobench::Bench()
                     .title("IOMMU_MapDifferentSizesBufferToNOC")
                     .unit("byte")
                     .epochs(NUM_EPOCHS)
                     .epochIterations(1);
    std::vector<Result> results;
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.

    static const long page_size = sysconf(_SC_PAGESIZE);
    const uint64_t MAPPING_SIZE_LIMIT = ONE_GIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device();
    for (uint64_t size = page_size; size <= MAPPING_SIZE_LIMIT; size *= 2) {
        bench.name(fmt::format("Map {} bytes", size)).batch(size);
        ankerl::nanobench::Result map_result(bench.config());
        bench.name(fmt::format("Unmap {} bytes", size));
        ankerl::nanobench::Result unmap_result(bench.config());
        for (int i = 0; i < NUM_EPOCHS; i++) {
            void* mapping =
                mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
            auto now = std::chrono::high_resolution_clock::now();
            pci_device->map_buffer_to_noc(mapping, size);
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

// Measure the time it takes to map three coexisting 1GiB buffers through IOMMU and configure iATU to make them
// visible to the device over NOC. This simulates the metal use case of several large buffers being mapped at the
// same time (only three because the iATU/NOC window is limited by the 32-bit address space and some small buffers
// are already mapped). The three buffers stay mapped simultaneously, so each successive mapping grows the IOMMU
// page table further.
TEST(MicrobenchmarkIOMMU, Map1GBPages) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    constexpr size_t NUM_PAGES = 3;
    const uint64_t MAPPING_SIZE = ONE_GIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    PCIDevice* pci_device = cluster->get_chip(CHIP_ID)->get_tt_device()->get_pci_device();

    std::array<void*, NUM_PAGES> mappings{};
    for (auto& mapping : mappings) {
        mapping =
            mmap(nullptr, MAPPING_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 1GiB buffer failed. Skipping test.";
        }
    }

    auto bench =
        ankerl::nanobench::Bench().title("IOMMU_Map1GBPages").unit("byte").epochs(NUM_EPOCHS).epochIterations(1);
    bench.name("Map 1G").batch(MAPPING_SIZE);
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 1G");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.
    for (int i = 0; i < NUM_EPOCHS; i++) {
        // Map all pages first (keeping them mapped simultaneously), timing each individually.
        for (auto* mapping : mappings) {
            auto now = std::chrono::high_resolution_clock::now();
            pci_device->map_buffer_to_noc(mapping, MAPPING_SIZE);
            auto end = std::chrono::high_resolution_clock::now();
            map_result.add(end - now, 1, pc);
        }
        for (auto* mapping : mappings) {
            auto now = std::chrono::high_resolution_clock::now();
            pci_device->unmap_for_dma(mapping, MAPPING_SIZE);
            auto end = std::chrono::high_resolution_clock::now();
            unmap_result.add(end - now, 1, pc);
        }
    }

    for (auto* mapping : mappings) {
        munmap(mapping, MAPPING_SIZE);
    }

    test::utils::export_results(bench.title(), std::vector<ankerl::nanobench::Result>{map_result, unmap_result});
}

// Same scenario as Map1GBPages (three coexisting 1GiB buffers mapped to NOC), but routed through the SysmemBuffer
// class to confirm there is no overhead compared to calling map_buffer_to_noc/unmap_for_dma directly.
TEST(MicrobenchmarkIOMMU, Map1GBPagesSysmemBuffers) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }
    if (std::getenv(OUTPUT_ENV_VAR) == nullptr) {
        GTEST_SKIP() << "This benchmark does not output results to std. output. Please define output path: "
                     << OUTPUT_ENV_VAR;
    }

    constexpr size_t NUM_PAGES = 3;
    const uint64_t MAPPING_SIZE = ONE_GIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    TTDevice* tt_device = cluster->get_chip(CHIP_ID)->get_tt_device();

    std::array<void*, NUM_PAGES> mappings{};
    for (auto& mapping : mappings) {
        mapping =
            mmap(nullptr, MAPPING_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 1GiB buffer failed. Skipping test.";
        }
    }

    auto bench = ankerl::nanobench::Bench()
                     .title("IOMMU_Map1GBPagesSysmemBuffers")
                     .unit("byte")
                     .epochs(NUM_EPOCHS)
                     .epochIterations(1);
    bench.name("Map 1G").batch(MAPPING_SIZE);
    ankerl::nanobench::Result map_result(bench.config());
    bench.name("Unmap 1G");
    ankerl::nanobench::Result unmap_result(bench.config());
    ankerl::nanobench::detail::PerformanceCounters pc;  // Empty perf. counters just to fill in args.

    std::array<std::unique_ptr<SysmemBuffer>, NUM_PAGES> sysmem_buffers{};
    for (int i = 0; i < NUM_EPOCHS; i++) {
        for (size_t page = 0; page < NUM_PAGES; page++) {
            auto now = std::chrono::high_resolution_clock::now();
            sysmem_buffers[page] = std::make_unique<SysmemBuffer>(tt_device, mappings[page], MAPPING_SIZE, true);
            auto end = std::chrono::high_resolution_clock::now();
            map_result.add(end - now, 1, pc);
        }
        for (auto& sysmem_buffer : sysmem_buffers) {
            auto now = std::chrono::high_resolution_clock::now();
            sysmem_buffer.reset();
            auto end = std::chrono::high_resolution_clock::now();
            unmap_result.add(end - now, 1, pc);
        }
    }

    for (auto* mapping : mappings) {
        munmap(mapping, MAPPING_SIZE);
    }

    test::utils::export_results(bench.title(), std::vector<ankerl::nanobench::Result>{map_result, unmap_result});
}
