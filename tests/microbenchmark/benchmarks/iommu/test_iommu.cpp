/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <gtest/gtest.h>
#include <sys/mman.h>

#include "common/microbenchmark_utils.hpp"
#include "tests/test_utils/device_test_utils.hpp"

using namespace tt::umd;

constexpr ChipId chip = 0;
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

std::tuple<double, double, double, double> get_results(
    std::vector<uint64_t>& map_times, std::vector<uint64_t>& unmap_times, uint64_t mapping_size) {
    sort(map_times.begin(), map_times.end());
    sort(unmap_times.begin(), unmap_times.end());

    uint64_t map_time_median = map_times[NUM_ITERATIONS / 2];
    uint64_t unmap_time_median = unmap_times[NUM_ITERATIONS / 2];

    uint64_t map_time_average = std::accumulate(map_times.begin(), map_times.end(), 0ULL) / NUM_ITERATIONS;
    uint64_t unmap_time_average = std::accumulate(unmap_times.begin(), unmap_times.end(), 0ULL) / NUM_ITERATIONS;

    const uint64_t num_pages = mapping_size / sysconf(_SC_PAGESIZE);
    double bw_map_mbs = (double)mapping_size / map_time_average;
    double bw_unmap_mbs = (double)mapping_size / unmap_time_average;

    return std::make_tuple(
        (double)map_time_average,
        test::utils::calc_speed(mapping_size, map_time_average),
        (double)unmap_time_average,
        test::utils::calc_speed(mapping_size, unmap_time_average));
}

void update_data(
    std::vector<std::vector<std::string>>& rows,
    const uint64_t mapping_size,
    std::vector<uint64_t>& map_times,
    std::vector<uint64_t>& unmap_times) {
    auto [avg_map_time, bw_map, avg_unmap_time, bw_unmap] = get_results(map_times, unmap_times, mapping_size);
    std::vector<std::string> row;
    row.push_back(test::utils::convert_double_to_string(mapping_size / sysconf(_SC_PAGESIZE)));
    row.push_back(test::utils::convert_double_to_string(mapping_size / one_mb));
    row.push_back(test::utils::convert_double_to_string(avg_map_time));
    row.push_back(test::utils::convert_double_to_string(bw_map));
    row.push_back(test::utils::convert_double_to_string(avg_unmap_time));
    row.push_back(test::utils::convert_double_to_string(bw_unmap));
    rows.push_back(row);
}

/**
 * Measure the time it takes to map buffers of different sizes through IOMMU.
 * This test allocates buffers of different size, starting from single page (usually 4KB) up to 1GB,
 * and measures the time it takes to map them through IOMMU. It prints out the time taken for each mapping
 * and the average time per page.
 */
TEST(MicrobenchmarkIOMMU, MapDifferentSizes) {
    guard_test_iommu();

    static const auto page_size = sysconf(_SC_PAGESIZE);

    const std::vector<uint64_t> mapping_sizes;

    std::cout << "Running IOMMU benchmark for different mapping sizes." << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Page size: " << page_size << " bytes." << std::endl;

    const uint64_t mapping_size_limit = 1ULL << 30;  // 1 GB

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    PCIDevice* pci_device = cluster->get_chip(chip)->get_tt_device()->get_pci_device().get();

    const std::vector<std::string> headers = {
        "Number of pages",
        "Mapping size (MB)",
        "Average map time (ns)",
        "Bandwidth map (MB/s)",
        "Average unmap time (ns)",
        "Bandwidth unmap (MB/s)"};

    std::vector<std::vector<std::string>> rows;

    for (uint64_t size = page_size; size <= mapping_size_limit; size *= 2) {
        std::vector<uint64_t> map_times;
        std::vector<uint64_t> unmap_times;

        for (int i = 0; i < NUM_ITERATIONS; i++) {
            void* mapping =
                mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
            auto now = std::chrono::steady_clock::now();
            uint64_t iova = pci_device->map_for_dma(mapping, size);
            auto end = std::chrono::steady_clock::now();
            map_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count());

            now = std::chrono::steady_clock::now();
            pci_device->unmap_for_dma(mapping, size);
            end = std::chrono::steady_clock::now();
            unmap_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count());

            munmap(mapping, size);
        }

        update_data(rows, size, map_times, unmap_times);
    }
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure the time it takes to map hugepages using IOMMU.
 * These should be different from regular buffers because it's guaranteed that hugepages are contiguous in memory.
 * Since contiguous memory has less entries in the IOMMU page table, we expect the mapping to be faster.
 */
TEST(MicrobenchmarkIOMMU, MapHugepages2M) {
    guard_test_iommu();

    static const auto page_size = sysconf(_SC_PAGESIZE);

    const std::vector<uint64_t> mapping_sizes;

    std::cout << "Running IOMMU benchmark for different mapping sizes." << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Page size: " << page_size << " bytes." << std::endl;

    const uint64_t mapping_size = 2 * (1ULL << 20);  // 2 MB

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    PCIDevice* pci_device = cluster->get_chip(chip)->get_tt_device()->get_pci_device().get();

    std::vector<uint64_t> map_times;
    std::vector<uint64_t> unmap_times;

    const std::vector<std::string> headers = {
        "Mapping size (MB)",
        "Average map time (ns)",
        "Bandwidth map (MB/s)",
        "Average unmap time (ns)",
        "Bandwidth unmap (MB/s)"};

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        void* mapping = mmap(
            0,
            mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (21 << MAP_HUGE_SHIFT),
            -1,
            0);

        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 2MB hugepage failed. Skipping test.";
        }

        auto now = std::chrono::steady_clock::now();
        uint64_t iova = pci_device->map_for_dma(mapping, mapping_size);
        auto end = std::chrono::steady_clock::now();
        map_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count());

        now = std::chrono::steady_clock::now();
        pci_device->unmap_for_dma(mapping, mapping_size);
        end = std::chrono::steady_clock::now();
        unmap_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count());

        munmap(mapping, mapping_size);
    }

    std::vector<std::vector<std::string>> rows;
    update_data(rows, mapping_size, map_times, unmap_times);

    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Measure the time it takes to map hugepages using IOMMU.
 * These should be different from regular buffers because it's guaranteed that hugepages are contiguous in memory.
 * Since contiguous memory has less entries in the IOMMU page table, we expect the mapping to be faster.
 */
TEST(MicrobenchmarkIOMMU, MapHugepages1G) {
    guard_test_iommu();

    static const auto page_size = sysconf(_SC_PAGESIZE);

    const std::vector<uint64_t> mapping_sizes;

    std::cout << "Running IOMMU benchmark for different mapping sizes." << std::endl;
    std::cout << "--------------------------------------------------------" << std::endl;
    std::cout << "Page size: " << page_size << " bytes." << std::endl;

    const uint64_t mapping_size = 1ULL << 30;  // 1 GB

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    PCIDevice* pci_device = cluster->get_chip(chip)->get_tt_device()->get_pci_device().get();

    std::vector<uint64_t> map_times;
    std::vector<uint64_t> unmap_times;

    const std::vector<std::string> headers = {
        "Mapping size (MB)",
        "Average map time (ns)",
        "Bandwidth map (MB/s)",
        "Average unmap time (ns)",
        "Bandwidth unmap (MB/s)"};

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        void* mapping = mmap(
            0,
            mapping_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (30 << MAP_HUGE_SHIFT),
            -1,
            0);
        if (mapping == MAP_FAILED) {
            GTEST_SKIP() << "Mapping 1GB hugepage failed. Skipping test.";
        }
        auto now = std::chrono::steady_clock::now();
        uint64_t iova = pci_device->map_for_dma(mapping, mapping_size);
        auto end = std::chrono::steady_clock::now();
        map_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count());

        now = std::chrono::steady_clock::now();
        pci_device->unmap_for_dma(mapping, mapping_size);
        end = std::chrono::steady_clock::now();
        unmap_times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count());

        munmap(mapping, mapping_size);
    }

    std::vector<std::vector<std::string>> rows;
    update_data(rows, mapping_size, map_times, unmap_times);

    test::utils::print_markdown_table_format(headers, rows);
}
