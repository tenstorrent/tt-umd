// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "l2cpu_test_utils.hpp"

// The rest is the layout of x280_experiments/01_dram_sum/kernel.S; keep it in sync.
// Data block in DRAM. The host plants A (+0x00) and B (+0x08); the kernel writes SUM and COUNT, and adds again
// whenever DOORBELL (+0x20, right after COUNT) changes.
constexpr uint64_t DATA_BLOCK = 0x0010'0000;
constexpr uint64_t DATA_SUM = DATA_BLOCK + 0x10;
constexpr uint64_t DATA_COUNT = DATA_BLOCK + 0x18;

// Status block in DRAM through the cached alias, one line, read through the L2CPU tile so it does not depend on the
// uncached mapping above. Words: magic, then A, B, SUM and COUNT as the kernel saw them, then mcause, mepc and mtval
// if it trapped. The magic is MAGIC_BOOT once hart 0 starts, MAGIC_ALIVE after each sum, or MAGIC_TRAP. The code
// must sit below it.
constexpr uint64_t STATUS_BLOCK = DRAM_CACHED + 0x0008'0000;
constexpr uint64_t MAGIC_BOOT = 0x0000'0280'B007'B007;
constexpr uint64_t MAGIC_ALIVE = 0x0000'0280'600D'600D;
constexpr uint64_t MAGIC_TRAP = 0x0000'0280'DEAD'DEAD;

constexpr auto KERNEL_TIMEOUT = std::chrono::seconds(5);
constexpr auto KERNEL_POLL_INTERVAL = std::chrono::milliseconds(50);

class L2CPULoaderTest : public L2CPUDeviceTest {};

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
    EXPECT_GE(num_enabled, 2U);  // can harvest two?

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
        const bool released = (l2cpu_reset >> (4 + idx)) & 1;  // 0 means held in reset

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

// One-shot: loads the ELF into local DRAM through the L2CPU tile, sets reset vectors and releases reset. Needs
// `tt-smi -r` before each run.
TEST_F(L2CPULoaderTest, Boot) {
    const char* elf_path = std::getenv("TT_UMD_L2CPU_ELF");
    if (elf_path == nullptr) {
        GTEST_SKIP() << "Set TT_UMD_L2CPU_ELF to a kernel ELF to run the one-shot boot test.";
    }

    // L2CPU tile to boot.
    const size_t idx = 0;
    const CoreCoord l2cpu_core(L2CPU_TILES[idx], CoreType::L2CPU, CoordSystem::NOC0);
    const CoreCoord dram_core(L2CPU_LOCAL_DRAM[idx], CoreType::DRAM, CoordSystem::NOC0);

    // 1. Pre-flight: tile not harvested, still held in reset, ELF fits below the status block. Nothing is written
    // before this passes.
    const ElfImage elf = read_elf(elf_path);
    ASSERT_NO_FATAL_FAILURE(check_l2cpu_bootable(tt_device_.get(), idx, elf, STATUS_BLOCK));

    // 2-6. Lower the PLL, load, set reset vectors, release reset, restore the PLL. After the load, clear the kernel's
    // outputs so values from an earlier run cannot look like a result: SUM, COUNT and DOORBELL in DRAM (A and B stay),
    // and the status block.
    ASSERT_NO_FATAL_FAILURE(boot_l2cpu(tt_device_.get(), idx, elf, [&] {
        const std::array<uint64_t, 3> zero_outputs = {};  // also clears COUNT and DOORBELL
        tt_device_->write_to_device(zero_outputs.data(), dram_core, DATA_SUM, sizeof(zero_outputs));
        const std::array<uint64_t, CACHE_LINE / sizeof(uint64_t)> zero_status = {};
        tt_device_->write_to_device(zero_status.data(), l2cpu_core, STATUS_BLOCK, sizeof(zero_status));
    }));

    // 7. Poll until COUNT in DRAM is non-zero (the first sum is there), or the status block says the kernel finished
    // a sum or trapped. The kernel has no free-running heartbeat: MAGIC_ALIVE is written once per sum.
    uint64_t count = 0;
    uint64_t magic = 0;
    const auto deadline = std::chrono::steady_clock::now() + KERNEL_TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        tt_device_->read_from_device(&count, dram_core, DATA_COUNT, sizeof(count));
        tt_device_->read_from_device(&magic, l2cpu_core, STATUS_BLOCK, sizeof(magic));
        if (count != 0 || magic == MAGIC_ALIVE || magic == MAGIC_TRAP) {
            break;
        }
        std::this_thread::sleep_for(KERNEL_POLL_INTERVAL);
    }

    // Read both blocks once more: the kernel writes COUNT before the status block, so they may have moved on.
    uint64_t sum = 0;
    tt_device_->read_from_device(&sum, dram_core, DATA_SUM, sizeof(sum));
    tt_device_->read_from_device(&count, dram_core, DATA_COUNT, sizeof(count));
    std::array<uint64_t, 8> status = {};  // magic, A, B, SUM, COUNT, mcause, mepc, mtval.
    tt_device_->read_from_device(status.data(), l2cpu_core, STATUS_BLOCK, sizeof(status));
    magic = status[0];

    log_info(
        tt::LogUMD,
        "SUM at DRAM {} 0x{:x} = 0x{:016x}, COUNT at 0x{:x} = {}.",
        L2CPU_LOCAL_DRAM[idx].str(),
        DATA_SUM,
        sum,
        DATA_COUNT,
        count);
    const char* state = magic == MAGIC_ALIVE  ? "alive"
                        : magic == MAGIC_TRAP ? "TRAP"
                        : magic == MAGIC_BOOT ? "started, no sum yet"
                                              : "no heartbeat";
    log_info(
        tt::LogUMD,
        "Status at X280 0x{:x}: magic = 0x{:016x} ({}); kernel saw A=0x{:x} B=0x{:x} SUM=0x{:x} COUNT={}.",
        STATUS_BLOCK,
        magic,
        state,
        status[1],
        status[2],
        status[3],
        status[4]);

    if (magic == MAGIC_TRAP) {
        log_warning(tt::LogUMD, "Kernel trapped: mcause={} mepc=0x{:x} mtval=0x{:x}.", status[5], status[6], status[7]);
    }
    EXPECT_NE(magic, MAGIC_TRAP) << "The kernel took an exception; see the log for mcause and mepc.";
    EXPECT_NE(magic, MAGIC_BOOT) << "Hart 0 started but never finished a sum: likely stuck on its first DRAM access.";
    EXPECT_FALSE(magic == MAGIC_ALIVE && count == 0)
        << "The kernel finished a sum but COUNT at " << L2CPU_LOCAL_DRAM[idx].str()
        << " is 0: X280 0x3000_0000 + N is not DRAM-tile address N.";
    EXPECT_NE(count, 0U) << "No sum within " << KERNEL_TIMEOUT.count() << " s.";
}
