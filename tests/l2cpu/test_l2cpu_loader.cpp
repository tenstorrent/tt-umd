// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <elf.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
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

// PLL4 setting. fbdiv is shared by all four tiles; postdivs[i] divides tile i's clock by (postdiv + 1).
struct L2CPUPll {
    uint32_t fbdiv;
    std::array<uint32_t, 4> postdivs;
};

// Reset must be released at <= 1/4 of the frequency the logic was timed at (~1.75 GHz). 200 MHz is from clock.py.
constexpr L2CPUPll PLL_200MHZ = {128, {15, 15, 15, 15}};

// PLL lock time after an fbdiv change, per SharePoint "PLL Programming".
constexpr auto PLL_LOCK_TIME = std::chrono::microseconds(300);

// CCACHE0_WAYENABLE, reached from the NOC at its X280 address. LIM holds (15 - WayEnable) ways of 128 KiB.
constexpr uint64_t L3_WAYENABLE = 0x0201'0008;
constexpr uint32_t LIM_WAY_KIB = 128;

// WayEnable value that makes the whole L3 a cache (no LIM), as tt-bh-linux boot.py sets before loading. LIM is not
// used: on hardware, host writes to unprimed LIM trip the L3 ECC check and leave the line failing on later reads.
constexpr uint32_t L3_ALL_WAYS = 0xF;

// Local DRAM bank in the X280 address space, cached alias. Host accesses go through the L2CPU tile and its L3, so they
// are coherent with the harts. tt-bh-linux boot.py loads OpenSBI at this base the same way.
constexpr uint64_t DRAM_CACHED = 0x4000'3000'0000;

// Host buffers are padded to whole cache lines.
constexpr uint64_t CACHE_LINE = 64;

// Per-hart reset vectors at X280 0x2001_0000 + 8 * hart, reached through the NOC "high alias" (X280 address +
// 0xFFFF_F7FE_DFF0_0000). Each is 64 bits, written low word first, like tt-bh-linux boot.py.
constexpr uint64_t RESET_VECTORS = 0xFFFF'F7FE'FFF1'0000;
constexpr uint32_t NUM_HARTS = 4;

// The rest is the layout of x280_experiments/01_dram_sum/kernel.S; keep it in sync.
// Each tile's local DRAM bank at one of its NOC0 endpoints, same index as L2CPU_TILES. The kernel reaches it at X280
// 0x3000_0000, which we read as DRAM-tile address 0 (ISA L2CPUTile/README.md; unverified on hardware).
const std::array<tt_xy_pair, 4> L2CPU_LOCAL_DRAM = {{{9, 3}, {9, 9}, {9, 5}, {9, 7}}};

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

L2CPUPll read_l2cpu_pll(TTDevice* tt_device, const CoreCoord& arc_core) {
    uint32_t cntl_1 = 0;
    uint32_t cntl_5 = 0;
    tt_device->read_from_device_reg(&cntl_1, arc_core, PLL_CNTL_1, sizeof(cntl_1));
    tt_device->read_from_device_reg(&cntl_5, arc_core, PLL_CNTL_5, sizeof(cntl_5));

    L2CPUPll pll = {cntl_1 >> 16, {}};
    for (size_t i = 0; i < pll.postdivs.size(); i++) {
        pll.postdivs[i] = (cntl_5 >> (8 * i)) & 0xFF;
    }
    return pll;
}

// Steps PLL4 one unit at a time, like tt-bh-linux clock.py: slowing postdivs first, then fbdiv, then speeding
// postdivs. No tile's clock goes above the higher of its start and target speeds.
void set_l2cpu_pll(TTDevice* tt_device, const CoreCoord& arc_core, const L2CPUPll& target) {
    uint32_t cntl_1 = 0;
    uint32_t cntl_5 = 0;
    tt_device->read_from_device_reg(&cntl_1, arc_core, PLL_CNTL_1, sizeof(cntl_1));
    tt_device->read_from_device_reg(&cntl_5, arc_core, PLL_CNTL_5, sizeof(cntl_5));

    // Moves postdiv i towards its target, but only when that is a step in the requested direction.
    auto step_postdiv = [&](size_t i, bool slow_down) {
        const uint32_t shift = 8 * i;
        uint32_t current = (cntl_5 >> shift) & 0xFF;
        while (slow_down ? current < target.postdivs[i] : current > target.postdivs[i]) {
            current = slow_down ? current + 1 : current - 1;
            cntl_5 = (cntl_5 & ~(0xFFU << shift)) | (current << shift);
            tt_device->write_to_device_reg(&cntl_5, arc_core, PLL_CNTL_5, sizeof(cntl_5));
        }
    };

    for (size_t i = 0; i < target.postdivs.size(); i++) {
        step_postdiv(i, true);
    }

    uint32_t fbdiv = cntl_1 >> 16;
    while (fbdiv != target.fbdiv) {
        fbdiv = fbdiv < target.fbdiv ? fbdiv + 1 : fbdiv - 1;
        cntl_1 = (cntl_1 & 0xFFFF) | (fbdiv << 16);
        tt_device->write_to_device_reg(&cntl_1, arc_core, PLL_CNTL_1, sizeof(cntl_1));
        std::this_thread::sleep_for(PLL_LOCK_TIME);
    }

    for (size_t i = 0; i < target.postdivs.size(); i++) {
        step_postdiv(i, false);
    }
}

struct ElfSegment {
    uint64_t addr;
    std::vector<uint8_t> data;
};

struct ElfImage {
    uint64_t entry;
    std::vector<ElfSegment> segments;
};

// Reads the PT_LOAD segments of a little-endian RISC-V ELF64. Each segment is zero-filled from filesz to memsz
// (that is .bss), then padded with zeros to whole cache lines.
ElfImage read_elf(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open " + path + ".");
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    Elf64_Ehdr ehdr{};
    if (bytes.size() < sizeof(ehdr)) {
        throw std::runtime_error(path + " is too short to be an ELF file.");
    }
    std::memcpy(&ehdr, bytes.data(), sizeof(ehdr));
    if (std::memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr.e_ident[EI_DATA] != ELFDATA2LSB || ehdr.e_machine != EM_RISCV) {
        throw std::runtime_error(path + " is not a little-endian RISC-V ELF64 file.");
    }

    ElfImage image = {ehdr.e_entry, {}};
    for (size_t i = 0; i < ehdr.e_phnum; i++) {
        const uint64_t phdr_offset = ehdr.e_phoff + i * ehdr.e_phentsize;
        Elf64_Phdr phdr{};
        if (phdr_offset + sizeof(phdr) > bytes.size()) {
            throw std::runtime_error(path + ": program header table is truncated.");
        }
        std::memcpy(&phdr, bytes.data() + phdr_offset, sizeof(phdr));
        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0) {
            continue;
        }
        if (phdr.p_filesz > phdr.p_memsz || phdr.p_offset + phdr.p_filesz > bytes.size()) {
            throw std::runtime_error(path + ": segment " + std::to_string(i) + " is truncated.");
        }

        const uint64_t padded_size = (phdr.p_memsz + CACHE_LINE - 1) / CACHE_LINE * CACHE_LINE;
        ElfSegment segment = {phdr.p_paddr, std::vector<uint8_t>(padded_size, 0)};
        std::copy_n(bytes.begin() + phdr.p_offset, phdr.p_filesz, segment.data.begin());
        image.segments.push_back(std::move(segment));
    }
    if (image.segments.empty()) {
        throw std::runtime_error(path + " has no loadable segments.");
    }
    return image;
}

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

    const SocDescriptor& soc_desc = tt_device_->get_soc_descriptor();
    const CoreCoord arc_core = soc_desc.get_cores(CoreType::ARC).at(0);

    // L2CPU tile to boot.
    const size_t idx = 0;
    const CoreCoord l2cpu_core(L2CPU_TILES[idx], CoreType::L2CPU, CoordSystem::NOC0);
    const CoreCoord dram_core(L2CPU_LOCAL_DRAM[idx], CoreType::DRAM, CoordSystem::NOC0);

    // 1. Pre-flight: tile not harvested, still held in reset, ELF fits below the status block. Nothing is written
    // before this passes.
    // Harvesting: telemetry and the SoC descriptor must agree.
    FirmwareTelemetryReader* telemetry = tt_device_->get_firmware_telemetry_reader();
    ASSERT_NE(telemetry, nullptr);
    ASSERT_TRUE(telemetry->is_entry_available(TelemetryTag::ENABLED_L2CPU));
    const uint32_t enabled_l2cpu = telemetry->read_entry(TelemetryTag::ENABLED_L2CPU);
    const size_t num_enabled = soc_desc.get_cores(CoreType::L2CPU).size();
    log_info(tt::LogUMD, "ENABLED_L2CPU = 0x{:x}, SoC descriptor lists {} L2CPU tiles.", enabled_l2cpu, num_enabled);
    EXPECT_EQ(std::bitset<4>(enabled_l2cpu).count(), num_enabled);
    EXPECT_GE(num_enabled, 2U);  // Up to two of the four tiles may be harvested.
    ASSERT_TRUE((enabled_l2cpu >> idx) & 1) << "L2CPU" << idx << " is harvested on this chip; pick another tile.";
    ASSERT_TRUE(soc_desc.is_core_of_type(L2CPU_TILES[idx], CoreType::L2CPU, CoordSystem::NOC0));
    ASSERT_TRUE(soc_desc.is_core_of_type(L2CPU_LOCAL_DRAM[idx], CoreType::DRAM, CoordSystem::NOC0))
        << "L2CPU" << idx << "'s local DRAM bank is harvested on this chip.";

    // Reset: bit (4 + idx) must still be 0. Releasing a tile again needs a chip reset. Bits [3:0] are tile resets.
    uint32_t l2cpu_reset = 0;
    tt_device_->read_from_device_reg(&l2cpu_reset, arc_core, L2CPU_RESET, sizeof(l2cpu_reset));
    log_info(tt::LogUMD, "L2CPU_RESET = 0x{:08x}.", l2cpu_reset);
    ASSERT_FALSE((l2cpu_reset >> (4 + idx)) & 1) << "L2CPU" << idx << " was already released (L2CPU_RESET = 0x"
                                                 << std::hex << l2cpu_reset << "). Run `tt-smi -r` first.";

    // Image: every segment must be whole, aligned lines in cached DRAM below the status block, and so must the entry.
    const ElfImage elf = read_elf(elf_path);
    for (const ElfSegment& segment : elf.segments) {
        const uint64_t segment_end = segment.addr + segment.data.size();
        ASSERT_TRUE(segment.addr % CACHE_LINE == 0 && segment.addr >= DRAM_CACHED && segment_end <= STATUS_BLOCK)
            << "Segment 0x" << std::hex << segment.addr << "-0x" << segment_end << " is not 64 B aligned inside [0x"
            << DRAM_CACHED << ", 0x" << STATUS_BLOCK << ").";
    }
    ASSERT_TRUE(elf.entry >= DRAM_CACHED && elf.entry < STATUS_BLOCK)
        << "Entry 0x" << std::hex << elf.entry << " is not inside [0x" << DRAM_CACHED << ", 0x" << STATUS_BLOCK << ").";
    log_info(
        tt::LogUMD,
        "Pre-flight ok: L2CPU{} held in reset, {} segments, entry 0x{:x}.",
        idx,
        elf.segments.size(),
        elf.entry);

    // 2. Lower the L2CPU PLL. Step 6 restores original_pll.
    const L2CPUPll original_pll = read_l2cpu_pll(tt_device_.get(), arc_core);
    log_info(
        tt::LogUMD,
        "PLL4 before: fbdiv={} postdivs=[{}, {}, {}, {}].",
        original_pll.fbdiv,
        original_pll.postdivs[0],
        original_pll.postdivs[1],
        original_pll.postdivs[2],
        original_pll.postdivs[3]);

    set_l2cpu_pll(tt_device_.get(), arc_core, PLL_200MHZ);

    const L2CPUPll lowered_pll = read_l2cpu_pll(tt_device_.get(), arc_core);
    ASSERT_EQ(lowered_pll.fbdiv, PLL_200MHZ.fbdiv);
    ASSERT_EQ(lowered_pll.postdivs, PLL_200MHZ.postdivs);
    log_info(tt::LogUMD, "PLL4 lowered to 200 MHz.");

    // 3. Make the whole L3 a cache, like tt-bh-linux boot.py, so host writes through the L2CPU tile land in cache
    // lines filled from DRAM, with valid ECC. Ways cannot be disabled again (SiFive manual, unverified here), so this
    // tile has no LIM until the next chip reset.
    tt_device_->write_to_device_reg(&L3_ALL_WAYS, l2cpu_core, L3_WAYENABLE, sizeof(L3_ALL_WAYS));
    uint32_t way_enable = 0;
    tt_device_->read_from_device_reg(&way_enable, l2cpu_core, L3_WAYENABLE, sizeof(way_enable));
    ASSERT_EQ(way_enable, L3_ALL_WAYS) << "L3 WayEnable reads back " << way_enable << ".";

    // Load ELF segments through the L2CPU tile at their X280 addresses, then read each back.
    for (const ElfSegment& segment : elf.segments) {
        tt_device_->write_to_device(segment.data.data(), l2cpu_core, segment.addr, segment.data.size());
        std::vector<uint8_t> readback(segment.data.size());
        tt_device_->read_from_device(readback.data(), l2cpu_core, segment.addr, readback.size());
        ASSERT_TRUE(readback == segment.data) << "Readback mismatch in segment at 0x" << std::hex << segment.addr;
        log_info(tt::LogUMD, "Loaded {} B at X280 0x{:x}, readback ok.", segment.data.size(), segment.addr);
    }

    // Clear the kernel's outputs so values from an earlier run cannot look like a result: SUM, COUNT and DOORBELL in
    // DRAM (A and B stay), and the status block.
    const std::array<uint64_t, 3> zero_outputs = {};  // also clears COUNT and DOORBELL
    tt_device_->write_to_device(zero_outputs.data(), dram_core, DATA_SUM, sizeof(zero_outputs));
    const std::array<uint64_t, CACHE_LINE / sizeof(uint64_t)> zero_status = {};
    tt_device_->write_to_device(zero_status.data(), l2cpu_core, STATUS_BLOCK, sizeof(zero_status));

    // 4. Point every hart's reset vector at the ELF entry, then read each back. These are registers, so the _reg
    // variants issue one 4 B access per word.
    const uint32_t entry_lo = static_cast<uint32_t>(elf.entry);
    const uint32_t entry_hi = static_cast<uint32_t>(elf.entry >> 32);
    for (uint32_t hart = 0; hart < NUM_HARTS; hart++) {
        const uint64_t vector_addr = RESET_VECTORS + 8 * hart;
        tt_device_->write_to_device_reg(&entry_lo, l2cpu_core, vector_addr, sizeof(entry_lo));
        tt_device_->write_to_device_reg(&entry_hi, l2cpu_core, vector_addr + 4, sizeof(entry_hi));

        uint32_t readback_lo = 0;
        uint32_t readback_hi = 0;
        tt_device_->read_from_device_reg(&readback_lo, l2cpu_core, vector_addr, sizeof(readback_lo));
        tt_device_->read_from_device_reg(&readback_hi, l2cpu_core, vector_addr + 4, sizeof(readback_hi));
        const uint64_t readback = (static_cast<uint64_t>(readback_hi) << 32) | readback_lo;
        ASSERT_EQ(readback, elf.entry) << "Hart " << hart << " reset vector reads back 0x" << std::hex << readback
                                       << ", wanted 0x" << elf.entry << ".";
    }
    log_info(tt::LogUMD, "Reset vectors of harts 0-{} -> 0x{:x}.", NUM_HARTS - 1, elf.entry);

    // 5. Release reset: set bit (4 + idx) in L2CPU_RESET, keeping every other bit. Re-read the register rather than
    // reuse the pre-flight value, so a change to another tile since then is not undone. This can happen only once
    // per chip reset.
    const uint32_t reset_bit = 1U << (4 + idx);
    uint32_t reset_before = 0;
    tt_device_->read_from_device_reg(&reset_before, arc_core, L2CPU_RESET, sizeof(reset_before));
    ASSERT_EQ(reset_before & reset_bit, 0U) << "L2CPU" << idx << " was released prematurely during the test.";

    const uint32_t reset_release = reset_before | reset_bit;
    tt_device_->write_to_device_reg(&reset_release, arc_core, L2CPU_RESET, sizeof(reset_release));

    uint32_t reset_after = 0;
    tt_device_->read_from_device_reg(&reset_after, arc_core, L2CPU_RESET, sizeof(reset_after));
    log_info(tt::LogUMD, "L2CPU_RESET 0x{:08x} -> 0x{:08x}: L2CPU{} released.", reset_before, reset_after, idx);
    ASSERT_EQ(reset_after, reset_release)
        << "L2CPU_RESET reads back 0x" << std::hex << reset_after << ", wanted 0x" << reset_release << ".";

    // 6. Raise the PLL back to the setting saved in step 2 (firmware leaves 800 MHz), then check it landed.
    set_l2cpu_pll(tt_device_.get(), arc_core, original_pll);

    const L2CPUPll restored_pll = read_l2cpu_pll(tt_device_.get(), arc_core);
    log_info(
        tt::LogUMD,
        "PLL4 restored: fbdiv={} postdivs=[{}, {}, {}, {}].",
        restored_pll.fbdiv,
        restored_pll.postdivs[0],
        restored_pll.postdivs[1],
        restored_pll.postdivs[2],
        restored_pll.postdivs[3]);
    ASSERT_EQ(restored_pll.fbdiv, original_pll.fbdiv);
    ASSERT_EQ(restored_pll.postdivs, original_pll.postdivs);

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
