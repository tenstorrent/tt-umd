// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Benchmarks for PcieProtocol DMA in isolation — mirrors the tests in test_pcie_dma.cpp
// which go through Cluster → TTDevice. Comparing results between the two validates that
// the PcieProtocol DMA implementation has no perf regression vs the TTDevice path.

#include <gtest/gtest.h>
#include <nanobench.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

namespace {

struct PcieProtocolFixture {
    std::unique_ptr<PcieProtocol> protocol;
    SocDescriptor soc_desc;

    PcieProtocolFixture() {
        std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        EXPECT_FALSE(pci_device_ids.empty()) << "No PCI devices found.";

        auto pci_device = std::make_unique<PCIDevice>(pci_device_ids.at(0));
        tt::ARCH arch = pci_device->get_arch();
        soc_desc = SocDescriptor(arch);
        protocol = std::make_unique<PcieProtocol>(std::move(pci_device));
    }

    tt_xy_pair get_core(CoreType type) const {
        auto cores = soc_desc.get_cores(type, CoordSystem::TRANSLATED);
        EXPECT_FALSE(cores.empty());
        return tt_xy_pair(cores[0].x, cores[0].y);
    }
};

}  // namespace

TEST(MicrobenchmarkPcieProtocolDMA, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("PcieProtocol_DMA_DRAM").unit("byte");
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

    PcieProtocolFixture fixture;
    if (fixture.protocol->get_pci_device()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    tt_xy_pair dram_core = fixture.get_core(CoreType::DRAM);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_write_to_device(pattern.data(), pattern.size(), dram_core, ADDRESS));
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_read_from_device(readback.data(), readback.size(), dram_core, ADDRESS));
        });
    }
    export_results(bench);
}

TEST(MicrobenchmarkPcieProtocolDMA, Tensix) {
    auto bench = ankerl::nanobench::Bench().title("PcieProtocol_DMA_Tensix").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB};

    PcieProtocolFixture fixture;
    if (fixture.protocol->get_pci_device()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    tt_xy_pair tensix_core = fixture.get_core(CoreType::TENSIX);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_write_to_device(pattern.data(), pattern.size(), tensix_core, ADDRESS));
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_read_from_device(readback.data(), readback.size(), tensix_core, ADDRESS));
        });
    }
    export_results(bench);
}

TEST(MicrobenchmarkPcieProtocolDMA, Ethernet) {
    auto bench = ankerl::nanobench::Bench().title("PcieProtocol_DMA_Ethernet").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 128 * ONE_KIB};

    PcieProtocolFixture fixture;
    if (fixture.protocol->get_pci_device()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    tt_xy_pair eth_core = fixture.get_core(CoreType::ETH);
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, write, {} bytes", batch_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_write_to_device(pattern.data(), pattern.size(), eth_core, ADDRESS));
        });
    }
    for (size_t batch_size : BATCH_SIZES) {
        std::vector<uint8_t> readback(batch_size);
        bench.batch(batch_size).name(fmt::format("DMA, read, {} bytes", batch_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_read_from_device(readback.data(), readback.size(), eth_core, ADDRESS));
        });
    }
    export_results(bench);
}

TEST(MicrobenchmarkPcieProtocolDMA, DRAMSweepSizes) {
    auto bench = ankerl::nanobench::Bench().title("PcieProtocol_DMA_DRAM_Sweep").unit("byte");
    const uint64_t ADDRESS = 0x0;

    PcieProtocolFixture fixture;
    if (fixture.protocol->get_pci_device()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    tt_xy_pair dram_core = fixture.get_core(CoreType::DRAM);
    const uint64_t LIMIT_BUF_SIZE = ONE_GIB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_write_to_device(pattern.data(), pattern.size(), dram_core, ADDRESS));
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_read_from_device(readback.data(), readback.size(), dram_core, ADDRESS));
        });
    }
    export_results(bench);
}

TEST(MicrobenchmarkPcieProtocolDMA, TensixSweepSizes) {
    auto bench = ankerl::nanobench::Bench().title("PcieProtocol_DMA_Tensix_Sweep").unit("byte");
    const uint64_t ADDRESS = 0x0;

    PcieProtocolFixture fixture;
    if (fixture.protocol->get_pci_device()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    tt_xy_pair tensix_core = fixture.get_core(CoreType::TENSIX);
    const uint64_t LIMIT_BUF_SIZE = ONE_MIB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_write_to_device(pattern.data(), pattern.size(), tensix_core, ADDRESS));
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_read_from_device(readback.data(), readback.size(), tensix_core, ADDRESS));
        });
    }
    export_results(bench);
}

TEST(MicrobenchmarkPcieProtocolDMA, EthernetSweepSizes) {
    auto bench = ankerl::nanobench::Bench().title("PcieProtocol_DMA_Ethernet_Sweep").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB

    PcieProtocolFixture fixture;
    if (fixture.protocol->get_pci_device()->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping PCIe DMA benchmarks for Blackhole.";
    }

    tt_xy_pair eth_core = fixture.get_core(CoreType::ETH);
    const uint64_t LIMIT_BUF_SIZE = 128 * ONE_KIB;
    for (uint64_t buf_size = 4; buf_size <= LIMIT_BUF_SIZE; buf_size *= 2) {
        std::vector<uint8_t> pattern(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, write, {} bytes", buf_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_write_to_device(pattern.data(), pattern.size(), eth_core, ADDRESS));
        });
        std::vector<uint8_t> readback(buf_size);
        bench.batch(buf_size).name(fmt::format("DMA, read, {} bytes", buf_size)).run([&]() {
            ASSERT_TRUE(fixture.protocol->dma_read_from_device(readback.data(), readback.size(), eth_core, ADDRESS));
        });
    }
    export_results(bench);
}
