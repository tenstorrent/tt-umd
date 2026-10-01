// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

// L2CPU tile index -> NOC0 location. The index is the bit in ENABLED_L2CPU and bit (4 + index) in L2CPU_RESET.
// This is not the order of blackhole::L2CPU_CORES_NOC0.
const std::array<tt_xy_pair, 4> L2CPU_TILES = {{{8, 3}, {8, 9}, {8, 5}, {8, 7}}};

// ARC reset unit. Bit (4 + index): 0 = harts held in reset, 1 = released. Bits [3:0] are reserved.
constexpr uint64_t L2CPU_RESET = blackhole::ARC_NOC_XBAR_ADDRESS_START + blackhole::ARC_RESET_UNIT_OFFSET + 0x14;

// L2CPU PLL (PLL4) in the ARC tile, from tt-bh-linux clock.py.
constexpr uint64_t PLL4_BASE = 0x8002'0500;
constexpr uint64_t PLL_CNTL_1 = PLL4_BASE + 0x04;  // Bytes: refdiv, postdiv (unused), fbdiv[15:0].
constexpr uint64_t PLL_CNTL_5 = PLL4_BASE + 0x14;  // Bytes: postdiv0..3, one per L2CPU tile.

// CCACHE0_WAYENABLE, reached from the NOC at its X280 address. LIM holds (15 - WayEnable) ways of 128 KiB.
constexpr uint64_t L3_WAYENABLE = 0x0201'0008;
constexpr uint32_t LIM_WAY_KIB = 128;

// Reset vectors and LIM base go here.

class L2CPULoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
        if (pci_device_ids.empty()) {
            GTEST_SKIP() << "No PCIe-attached Tenstorrent device found.";
        }
        if (PCIDevice::get_pcie_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "L2CPU tiles exist only on Blackhole.";
        }

        tt_device_ = TTDevice::create(pci_device_ids.at(0));
        tt_device_->set_power_state(TTDevice::PowerState::BUSY);
        tt_device_->init_tt_device();
    }

    void TearDown() override {
        if (tt_device_) {
            tt_device_->set_power_state(TTDevice::PowerState::IDLE);
        }
    }

    std::unique_ptr<TTDevice> tt_device_;
};

// Read-only: harvesting, L2CPU_RESET state, L3 WayEnable, PLL. Safe to run any number of times.
TEST_F(L2CPULoaderTest, Probe) {
    const SocDescriptor& soc_desc = tt_device_->get_soc_descriptor();
    const CoreCoord arc_core = soc_desc.get_cores(CoreType::ARC).at(0);

    // Harvesting: telemetry and the SoC descriptor must agree.
    FirmwareTelemetryReader* telemetry = tt_device_->get_firmware_telemetry_reader();
    ASSERT_NE(telemetry, nullptr);
    ASSERT_TRUE(telemetry->is_entry_available(TelemetryTag::ENABLED_L2CPU));
    const uint32_t enabled_l2cpu = telemetry->read_entry(TelemetryTag::ENABLED_L2CPU);
    const size_t num_enabled = soc_desc.get_cores(CoreType::L2CPU).size();
    log_info(tt::LogUMD, "ENABLED_L2CPU = 0x{:x}, SoC descriptor lists {} L2CPU tiles.", enabled_l2cpu, num_enabled);
    EXPECT_EQ(std::bitset<4>(enabled_l2cpu).count(), num_enabled);
    EXPECT_GE(num_enabled, 2U);

    // Reset state of all four tiles, one register.
    uint32_t l2cpu_reset = 0;
    tt_device_->read_from_device_reg(&l2cpu_reset, arc_core, L2CPU_RESET, sizeof(l2cpu_reset));
    log_info(tt::LogUMD, "L2CPU_RESET = 0x{:08x}.", l2cpu_reset);

    // PLL4 raw fields.
    uint32_t pll_cntl_1 = 0;
    uint32_t pll_cntl_5 = 0;
    tt_device_->read_from_device_reg(&pll_cntl_1, arc_core, PLL_CNTL_1, sizeof(pll_cntl_1));
    tt_device_->read_from_device_reg(&pll_cntl_5, arc_core, PLL_CNTL_5, sizeof(pll_cntl_5));
    log_info(
        tt::LogUMD,
        "PLL4: refdiv={} fbdiv={} postdivs=[{}, {}, {}, {}].",
        pll_cntl_1 & 0xFF,
        pll_cntl_1 >> 16,
        pll_cntl_5 & 0xFF,
        (pll_cntl_5 >> 8) & 0xFF,
        (pll_cntl_5 >> 16) & 0xFF,
        pll_cntl_5 >> 24);

    for (size_t idx = 0; idx < L2CPU_TILES.size(); idx++) {
        const tt_xy_pair& tile = L2CPU_TILES[idx];
        const bool enabled = (enabled_l2cpu >> idx) & 1;
        const bool released = (l2cpu_reset >> (4 + idx)) & 1;

        const auto clock_tag = static_cast<uint8_t>(TelemetryTag::L2CPUCLK0 + idx);
        const std::string clock_mhz =
            telemetry->is_entry_available(clock_tag) ? std::to_string(telemetry->read_entry(clock_tag)) : "n/a";

        log_info(
            tt::LogUMD,
            "L2CPU{} {}: {}, {}, clock {} MHz.",
            idx,
            tile.str(),
            enabled ? "enabled" : "HARVESTED",
            released ? "RELEASED (used since last tt-smi -r)" : "held in reset",
            clock_mhz);

        EXPECT_EQ(enabled, soc_desc.is_core_of_type(tile, CoreType::L2CPU, CoordSystem::NOC0));
        if (!enabled) {
            continue;
        }

        uint32_t way_enable = 0;
        tt_device_->read_from_device_reg(
            &way_enable, CoreCoord(tile, CoreType::L2CPU, CoordSystem::NOC0), L3_WAYENABLE, sizeof(way_enable));
        log_info(
            tt::LogUMD,
            "L2CPU{} L3 WayEnable = {} -> LIM {} KiB.",
            idx,
            way_enable,
            (15 - (way_enable & 0xF)) * LIM_WAY_KIB);
    }
}

// One-shot: loads the ELF into LIM, sets reset vectors and releases reset. Needs `tt-smi -r` before each run.
TEST_F(L2CPULoaderTest, Boot) {
    const char* elf_path = std::getenv("TT_UMD_L2CPU_ELF");
    if (elf_path == nullptr) {
        GTEST_SKIP() << "Set TT_UMD_L2CPU_ELF to a kernel ELF to run the one-shot boot test.";
    }

    // 1. Pre-flight: tile not harvested, still held in reset.
    // 2. Lower the L2CPU PLL.
    // 3. Load ELF segments into LIM.
    // 4. Write per-hart reset vectors, read them back.
    // 5. Release reset (bit 4+idx in L2CPU_RESET).
    // 6. Raise the PLL.
    // 7. Poll for the kernel's result / heartbeat.
}
