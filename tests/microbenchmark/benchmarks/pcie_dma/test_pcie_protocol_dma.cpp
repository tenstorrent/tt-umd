// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Benchmarks for PcieProtocol DMA in isolation — mirrors the tests in test_pcie_dma.cpp
// which go through Cluster → TTDevice. Comparing results between the two validates that
// the PcieProtocol DMA implementation has no perf regression vs the TTDevice path.

#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/protocol/pcie_protocol.hpp"
#include "umd/device/types/arch.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

// Sends a Wormhole ARC message via BAR0 registers and waits for completion.
// Replicates the protocol from WormholeArcMessenger::send_message without requiring TTDevice.
// Returns the ARC exit code, and optionally writes the return value from SCRATCH_RES0.
static uint32_t send_wh_arc_msg(PCIDevice* pci_device, uint32_t msg_code, uint32_t arg = 0, uint32_t* ret = nullptr) {
    volatile uint8_t* bar0 = static_cast<volatile uint8_t*>(pci_device->bar0);

    auto read32 = [bar0](uint64_t off) -> uint32_t { return *reinterpret_cast<volatile uint32_t*>(bar0 + off); };
    auto write32 = [bar0](uint64_t off, uint32_t val) { *reinterpret_cast<volatile uint32_t*>(bar0 + off) = val; };

    constexpr uint64_t XBAR = wormhole::ARC_APB_BAR0_XBAR_OFFSET_START;

    write32(XBAR + wormhole::ARC_RESET_SCRATCH_RES0_OFFSET, arg);
    write32(XBAR + wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET, msg_code);

    // Trigger FW interrupt via ARC_MISC_CNTL bit 16.
    uint32_t misc = read32(XBAR + wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET);
    if (misc & (1 << 16)) {
        return 1;  // Interrupt bit already set — FW not ready.
    }
    write32(XBAR + wormhole::ARC_RESET_ARC_MISC_CNTL_OFFSET, misc | (1 << 16));

    // Poll for ARC response.
    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t status = read32(XBAR + wormhole::ARC_RESET_SCRATCH_STATUS_OFFSET);
        if ((status & 0xffff) == (msg_code & 0xff)) {
            if (ret != nullptr) {
                *ret = read32(XBAR + wormhole::ARC_RESET_SCRATCH_RES0_OFFSET);
            }
            return (status & 0xffff0000) >> 16;
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(10)) {
            return 0xffffffff;  // Timeout.
        }
    }
}

// Sets AICLK to max frequency via ARC GO_BUSY and waits for the clock to ramp up.
// Only supports Wormhole — Blackhole uses a queue-based ARC protocol that requires TTDevice.
static void set_power_state_busy(PCIDevice* pci_device) {
    if (pci_device->get_arch() != tt::ARCH::WORMHOLE_B0) {
        return;
    }

    constexpr uint32_t GO_BUSY_MSG =
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::ARC_GO_BUSY);
    ASSERT_EQ(send_wh_arc_msg(pci_device, GO_BUSY_MSG), 0u) << "ARC GO_BUSY message failed";

    // Poll AICLK until it stabilizes at max frequency.
    constexpr uint32_t GET_AICLK_MSG =
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::GET_AICLK);
    auto start = std::chrono::steady_clock::now();
    uint32_t prev = 0;
    uint32_t curr = 0;
    ASSERT_EQ(send_wh_arc_msg(pci_device, GET_AICLK_MSG, 0, &curr), 0u);
    while (curr != prev) {
        prev = curr;
        ASSERT_EQ(send_wh_arc_msg(pci_device, GET_AICLK_MSG, 0, &curr), 0u);
        ASSERT_LT(
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count(), 10)
            << "Timed out waiting for AICLK to stabilize";
    }
}

struct PcieProtocolFixture {
    tt::ARCH arch = tt::ARCH::Invalid;
    std::unique_ptr<PcieProtocol> protocol;
    SocDescriptor soc_desc;

    PcieProtocolFixture() {
        std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        EXPECT_FALSE(pci_device_ids.empty()) << "No PCI devices found.";

        auto pci_device = std::make_unique<PCIDevice>(pci_device_ids.at(0));
        arch = pci_device->get_arch();
        if (arch == tt::ARCH::BLACKHOLE) {
            return;  // BH skipped — tests check is_blackhole() and GTEST_SKIP.
        }
        soc_desc = SocDescriptor(arch);
        set_power_state_busy(pci_device.get());
        protocol = std::make_unique<PcieProtocol>(std::move(pci_device));
    }

    bool is_blackhole() const { return arch == tt::ARCH::BLACKHOLE; }

    tt_xy_pair get_core(CoreType type) const {
        auto cores = soc_desc.get_cores(type, CoordSystem::TRANSLATED);
        EXPECT_FALSE(cores.empty());
        return tt_xy_pair(cores[0].x, cores[0].y);
    }
};

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
    if (fixture.is_blackhole()) {
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
    if (fixture.is_blackhole()) {
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
    if (fixture.is_blackhole()) {
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
    if (fixture.is_blackhole()) {
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
    if (fixture.is_blackhole()) {
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
    if (fixture.is_blackhole()) {
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
