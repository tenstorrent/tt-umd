/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <sys/mman.h>

#include "common/microbenchmark_utils.h"
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
const uint32_t one_mb = 1 << 20;
const uint32_t one_gb = 1 << 30;
const uint32_t NUM_ITERATIONS = 1000;

static inline std::pair<double, double> perf_read_write(
    const uint32_t buf_size,
    const uint32_t num_iterations,
    const std::unique_ptr<Cluster>& cluster,
    const CoreCoord core) {
    std::vector<uint8_t> pattern(buf_size);
    test_utils::fill_with_random_bytes(&pattern[0], pattern.size());

    auto now = std::chrono::steady_clock::now();
    for (int i = 0; i < num_iterations; i++) {
        cluster->dma_write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);
    }
    auto end = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
    double wr_bw = test::utils::calc_speed(num_iterations * pattern.size(), ns);

    std::vector<uint8_t> readback(buf_size, 0x0);
    now = std::chrono::steady_clock::now();
    for (int i = 0; i < num_iterations; i++) {
        cluster->dma_read_from_device(readback.data(), readback.size(), chip, core, 0x0);
    }
    end = std::chrono::steady_clock::now();
    ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
    double rd_bw = test::utils::calc_speed(num_iterations * readback.size(), ns);

    return std::make_pair(wr_bw, rd_bw);
}

static inline std::pair<double, double> perf_sysmem_read_write(
    const uint32_t buf_size,
    const uint32_t num_iterations,
    const std::unique_ptr<SysmemBuffer>& sysmem_buffer,
    const CoreCoord core) {
    double wr_bw;
    double rd_bw;
    {
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            sysmem_buffer->dma_write_to_device(0, one_mb, core, 0);
        }
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        wr_bw = test::utils::calc_speed(one_mb * NUM_ITERATIONS, ns);
    }

    {
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            sysmem_buffer->dma_read_from_device(0, one_mb, core, 0);
        }
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        rd_bw = test::utils::calc_speed(one_mb * NUM_ITERATIONS, ns);
    }

    return std::make_pair(wr_bw, rd_bw);
}

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
 * Test the PCIe DMA controller by using it to write random fixed-size pattern
 * to address of Tensix core, then reading them back and verifying.
 */
TEST(MicrobenchmarkPCIeDMA, DMATensix) {
    const std::vector<uint32_t> sizes = {
        1 * one_mb,
    };

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> Device Tensix L1 (MB/s)",
        "Device Tensix L1 -> Host (MB/s)",
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device(tt_device_params{});

    std::vector<std::vector<std::string>> rows;

    for (uint32_t buf_size : sizes) {
        std::vector<std::string> row;
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        auto [wr_bw, rd_bw] = perf_read_write(buf_size, NUM_ITERATIONS, cluster, tensix_core);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }

    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Microbenchmark to test the PCIe DMA reads/write to Tensix by sweeping through
 * different buffer sizes, starting from 4 bytes up to 1 MB.
 */
TEST(MicrobenchmarkPCIeDMA, TensixSweepSizes) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];
    cluster->start_device(tt_device_params{});

    const uint64_t limit_buf_size = one_mb;

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> Device Tensix L1 (MB/s)",
        "Device Tensix L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;
    for (uint64_t buf_size = 4; buf_size <= limit_buf_size; buf_size *= 2) {
        std::vector<std::string> row;
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        auto [wr_bw, rd_bw] = perf_read_write(buf_size, NUM_ITERATIONS, cluster, tensix_core);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }

    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Microbenchmark to test the PCIe DMA reads/writes to DRAM by sweeping through
 * different buffer sizes, starting from 4 bytes up to 1 MB.
 */
TEST(MicrobenchmarkPCIeDMA, DramSweepSizes) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device(tt_device_params{});

    const uint64_t limit_buf_size = one_gb;

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> Device DRAM (MB/s)",
        "Device DRAM -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;

    for (uint64_t buf_size = 4; buf_size <= limit_buf_size; buf_size *= 2) {
        std::vector<std::string> row;
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        auto [wr_bw, rd_bw] = perf_read_write(buf_size, NUM_ITERATIONS, cluster, dram_core);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }

    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size pattern
 * to address 0 of DRAM core, then reading them back and verifying.
 */
TEST(MicrobenchmarkPCIeDMA, DMADram) {
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
        512 * one_mb,
        1024 * one_mb,
    };

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::DRAM)[0];
    cluster->start_device(tt_device_params{});

    const std::vector<std::string> headers = {
        "Size (MB)",
        "DMA: Host -> Device DRAM (MB/s)",
        "DMA: Device DRAM -> Host (MB/s)",
    };
    std::vector<std::vector<std::string>> rows;
    for (uint32_t buf_size : sizes) {
        std::vector<std::string> row;
        row.push_back(test::utils::convert_double_to_string((double)buf_size / one_mb));
        auto [wr_bw, rd_bw] = perf_read_write(buf_size, NUM_ITERATIONS, cluster, dram_core);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
        row.push_back(test::utils::convert_double_to_string(rd_bw));
        rows.push_back(row);
    }

    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size pattern
 * to address 0 of Tensix core, then reading them back and verifying.
 * This test measures BW of IO using PCIe DMA engine without overhead of copying data into DMA buffer.
 */
TEST(MicrobenchmarkPCIeDMA, DMATensixZeroCopy) {
    guard_test_iommu();

    const uint32_t NUM_ITERATIONS = 1000;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    const chip_id_t mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const uint32_t one_mb = 1 << 20;
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(2 * one_mb);

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> Device Tensix L1 (MB/s)",
        "Device Tensix L1 -> Host (MB/s)",
    };

    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    auto [wr_bw, rd_bw] = perf_sysmem_read_write(one_mb, NUM_ITERATIONS, sysmem_buffer, tensix_core);
    row.push_back(test::utils::convert_double_to_string((double)one_mb / one_mb));
    row.push_back(test::utils::convert_double_to_string(wr_bw));
    row.push_back(test::utils::convert_double_to_string(rd_bw));
    rows.push_back(row);

    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * This test measures BW of IO using PCIe DMA engine where user buffer is mapped through IOMMU
 * and no copying is done. It uses SysmemManager to map the buffer and then uses DMA to transfer data
 * to and from the device.
 */
TEST(MicrobenchmarkPCIeDMA, DMATensixMapBufferZeroCopy) {
    guard_test_iommu();

    const uint32_t NUM_ITERATIONS = 100;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    const chip_id_t mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const uint32_t one_mb = 1 << 20;

    const std::vector<std::string> headers = {
        "Size (MB)",
        "Host -> Device Tensix L1 (MB/s)",
        "Device Tensix L1 -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    row.push_back(test::utils::convert_double_to_string((double)one_mb / one_mb));

    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];
    {
        void* mapping =
            mmap(nullptr, one_mb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping, one_mb);
            sysmem_buffer->dma_write_to_device(0, one_mb, tensix_core, 0);
        }
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        double wr_bw = test::utils::calc_speed(one_mb * NUM_ITERATIONS, ns);
        row.push_back(test::utils::convert_double_to_string(wr_bw));
    }

    {
        void* mapping =
            mmap(nullptr, one_mb, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < NUM_ITERATIONS; i++) {
            std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping, one_mb);
            sysmem_buffer->dma_read_from_device(0, one_mb, tensix_core, 0);
        }
        auto end = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - now).count();
        double rd_bw = test::utils::calc_speed(one_mb * NUM_ITERATIONS, ns);
        row.push_back(test::utils::convert_double_to_string(rd_bw));
    }
    rows.push_back(row);
    test::utils::print_markdown_table_format(headers, rows);
}

/**
 * This test measures BW of IO using PCIe DMA engine where user buffer is mapped through IOMMU
 * and no copying is done. It uses SysmemManager to map the buffer and then uses DMA to transfer data
 * to and from the device.
 */
TEST(MicrobenchmarkPCIeDMA, DMADRAMZeroCopy) {
    guard_test_iommu();

    const uint32_t NUM_ITERATIONS = 10;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });

    const chip_id_t mmio_chip = *cluster->get_target_mmio_device_ids().begin();

    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();

    const uint32_t one_mb = 1 << 20;
    const uint32_t one_hundred_mb = 100 * one_mb;
    const uint32_t two_hundred_mb = 200 * one_mb;
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(two_hundred_mb);

    const std::vector<std::string> headers = {
        "Size (MB)",
        "DMA: Host -> Device DRAM (MB/s)",
        "DMA: Device DRAM -> Host (MB/s)",
    };

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;

    const CoreCoord dram_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::DRAM)[0];
    auto [wr_bw, rd_bw] = perf_sysmem_read_write(one_mb, NUM_ITERATIONS, sysmem_buffer, dram_core);
    row.push_back(test::utils::convert_double_to_string((double)one_mb / one_mb));
    row.push_back(test::utils::convert_double_to_string(wr_bw));
    row.push_back(test::utils::convert_double_to_string(rd_bw));
    rows.push_back(row);
    test::utils::print_markdown_table_format(headers, rows);
}
