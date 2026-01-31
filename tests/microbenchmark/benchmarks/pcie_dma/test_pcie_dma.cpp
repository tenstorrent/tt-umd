// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>
#include <sys/mman.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "fmt/format.h"
#include "umd/device/chip_helpers/sysmem_buffer.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include <fmt/format.h>

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

TEST(MicrobenchmarkPCIeDMA, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("DMA_DRAM").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        4,
        8,
        16,
        32,
        1 * ONE_KIB,
        2 * ONE_KIB,
        4 * ONE_KIB,
        8 * ONE_KIB,
        16 * ONE_KIB,
        32 * ONE_KIB,
        1 * ONE_MIB,
        2 * ONE_MIB,
        4 * ONE_MIB,
        8 * ONE_MIB,
        16 * ONE_MIB,
        32 * ONE_MIB,
        1 * ONE_GIB,
    };
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkPCIeDMA, Tensix) {
    auto bench = ankerl::nanobench::Bench().title("DMA_Tensix").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkPCIeDMA, Ethernet) {
    auto bench = ankerl::nanobench::Bench().title("DMA_Ethernet").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 128 * ONE_KIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    const CoreCoord eth_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::ETH)[0];
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkPCIeDMA, DRAMSweepSizes) {
    auto bench = ankerl::nanobench::Bench().title("DMA_DRAM_Sweep").unit("byte");
    const uint64_t ADDRESS = 0x0;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    const uint64_t LIMIT_BUF_SIZE = ONE_GIB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, dram_core, ADDRESS);
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, dram_core, ADDRESS);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkPCIeDMA, TensixSweepSizes) {
    auto bench = ankerl::nanobench::Bench().title("DMA_Tensix_Sweep").unit("byte");
    const uint64_t ADDRESS = 0x0;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    const uint64_t LIMIT_BUF_SIZE = ONE_MIB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, tensix_core, ADDRESS);
        });
    }
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkPCIeDMA, EthernetSweepSizes) {
    auto bench = ankerl::nanobench::Bench().title("DMA_Ethernet_Sweep").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }
    const CoreCoord eth_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::ETH)[0];
    const uint64_t LIMIT_BUF_SIZE = 128 * ONE_KIB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            cluster->dma_write_to_device(pattern.data(), pattern.size(), CHIP_ID, eth_core, ADDRESS);
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            cluster->dma_read_from_device(readback.data(), readback.size(), CHIP_ID, eth_core, ADDRESS);
        });
    }
    test::utils::export_results(bench);
}

// This test measures bandwidth of IO using PCIe DMA engine where user buffer is mapped through IOMMU
// and no copying is done. It uses SysmemManager to map the buffer and then uses DMA to transfer data
// to and from the device.
TEST(MicrobenchmarkPCIeDMA, DRAMZeroCopy) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }

    auto bench = ankerl::nanobench::Bench().title("DMA_DRAM_ZeroCopy").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const size_t BUFFER_SIZE = ONE_MIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(200 * ONE_MIB);
    const CoreCoord dram_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::DRAM)[0];

    bench.batch(BUFFER_SIZE).name(fmt::format("DMA, write, {} bytes", BUFFER_SIZE)).run([&]() {
        sysmem_buffer->dma_write_to_device(0, BUFFER_SIZE, dram_core, ADDRESS);
    });
    bench.batch(BUFFER_SIZE).name(fmt::format("DMA, read, {} bytes", BUFFER_SIZE)).run([&]() {
        sysmem_buffer->dma_read_from_device(0, BUFFER_SIZE, dram_core, ADDRESS);
    });
    test::utils::export_results(bench);
}

// Test the PCIe DMA controller by using it to write random fixed-size pattern
// to address 0 of Tensix core, then reading them back and verifying.
// This test measures bandwidth of IO using PCIe DMA engine without overhead of copying data into DMA buffer.
TEST(MicrobenchmarkPCIeDMA, TensixZeroCopy) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }

    auto bench = ankerl::nanobench::Bench().title("DMA_Tensix_ZeroCopy").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const size_t BUFFER_SIZE = ONE_MIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();
    std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->allocate_sysmem_buffer(2 * ONE_MIB);
    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];

    bench.batch(BUFFER_SIZE).name(fmt::format("DMA, write, {} bytes", BUFFER_SIZE)).run([&]() {
        sysmem_buffer->dma_write_to_device(0, BUFFER_SIZE, tensix_core, ADDRESS);
    });
    bench.batch(BUFFER_SIZE).name(fmt::format("DMA, read, {} bytes", BUFFER_SIZE)).run([&]() {
        sysmem_buffer->dma_read_from_device(0, BUFFER_SIZE, tensix_core, ADDRESS);
    });
    test::utils::export_results(bench);
}

// This test measures bandwidth of IO using PCIe DMA engine where user buffer is mapped through IOMMU
// and no copying is done. It uses SysmemManager to map the buffer and then uses DMA to transfer data
// to and from the device.
TEST(MicrobenchmarkPCIeDMA, TensixMapBufferZeroCopy) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (!PCIDevice(pci_device_ids.at(0)).is_iommu_enabled()) {
        GTEST_SKIP() << "Skipping test since IOMMU is not enabled on the system.";
    }

    auto bench = ankerl::nanobench::Bench().title("DMA_Tensix_MapBuffer_ZeroCopy").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const size_t BUFFER_SIZE = ONE_MIB;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = 0,
    });
    if (cluster->get_cluster_description()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }
    const ChipId mmio_chip = *cluster->get_target_mmio_device_ids().begin();
    SysmemManager* sysmem_manager = cluster->get_chip(mmio_chip)->get_sysmem_manager();
    void* mapping =
        mmap(nullptr, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(CoreType::TENSIX)[0];

    bench.batch(BUFFER_SIZE).name(fmt::format("DMA, write, {} bytes", BUFFER_SIZE)).run([&]() {
        std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping, BUFFER_SIZE);
        sysmem_buffer->dma_write_to_device(0, BUFFER_SIZE, tensix_core, ADDRESS);
    });
    bench.batch(BUFFER_SIZE).name(fmt::format("DMA, read, {} bytes", BUFFER_SIZE)).run([&]() {
        std::unique_ptr<SysmemBuffer> sysmem_buffer = sysmem_manager->map_sysmem_buffer(mapping, BUFFER_SIZE);
        sysmem_buffer->dma_read_from_device(0, BUFFER_SIZE, tensix_core, ADDRESS);
    });
    munmap(mapping, BUFFER_SIZE);
    test::utils::export_results(bench);
}
