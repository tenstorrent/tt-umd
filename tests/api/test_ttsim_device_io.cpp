// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Tests that verify write_to_device/read_from_device (TLB-based path) and
// tile_wr_bytes/tile_rd_bytes (direct simulator API) produce consistent results.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/simulation_device_factory.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class TTSimDeviceIOFixture : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not set. Skipping TTSim device IO tests.";
        }
        auto device = create_simulation_tt_device(simulator_path);
        auto* sim_device = dynamic_cast<TTSimTTDevice*>(device.get());
        if (sim_device == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not point to a TTSimTTDevice. Skipping TTSim device IO tests.";
        }
        tt_device.reset(sim_device);
        device.release();  // NOLINT(bugprone-unused-return-value)

        const tt::ARCH arch = tt_device->get_soc_descriptor().arch;
        if (arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "tile_wr_bytes/tile_rd_bytes are no longer supported on TTSim for arch "
                         << tt::arch_to_str(arch);
        }
    }

    void TearDown() override {
        if (tt_device) {
            tt_device.reset();
        }
    }

    // Build a byte vector of `size` elements where byte[i] = fn(i).
    template <typename Fn>
    static std::vector<uint8_t> make_pattern(size_t size, Fn fn) {
        std::vector<uint8_t> v(size);
        for (size_t i = 0; i < size; ++i) {
            v[i] = static_cast<uint8_t>(fn(i));
        }
        return v;
    }

    std::unique_ptr<TTSimTTDevice> tt_device;
};

// ---------------------------------------------------------------------------
// write_to_device (TLB path) → tile_rd_bytes (direct path)
// ---------------------------------------------------------------------------

// Write a pattern via the TLB path, read it back via tile_rd_bytes and compare.
TEST_F(TTSimDeviceIOFixture, WriteToDeviceReadByTileRdBytes) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 1024;
    constexpr uint64_t addr = 0x100;

    auto write_data = make_pattern(data_size, [](size_t i) { return i % 256; });

    tt_device->write_to_device(write_data.data(), core, addr, data_size);

    std::vector<uint8_t> read_data(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_data.data(), data_size);

    EXPECT_EQ(write_data, read_data);
}

// Write zeros via TLB then a pattern via TLB; confirm tile_rd_bytes sees each state.
TEST_F(TTSimDeviceIOFixture, WriteToDeviceZeroThenPatternReadByTileRdBytes) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x200;

    std::vector<uint8_t> zeros(data_size, 0);
    auto pattern = make_pattern(data_size, [](size_t i) { return i % 256; });

    // First write zeros.
    tt_device->write_to_device(zeros.data(), core, addr, data_size);

    std::vector<uint8_t> read_zeros(data_size, 0xFF);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_zeros.data(), data_size);
    EXPECT_EQ(zeros, read_zeros) << "tile_rd_bytes should see zeros after write_to_device wrote zeros";

    // Then write the pattern.
    tt_device->write_to_device(pattern.data(), core, addr, data_size);

    std::vector<uint8_t> read_pattern(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_pattern.data(), data_size);
    EXPECT_EQ(pattern, read_pattern) << "tile_rd_bytes should see pattern after write_to_device wrote pattern";
}

// Large write (4 KB) via TLB path, read back via tile_rd_bytes.
TEST_F(TTSimDeviceIOFixture, LargeWriteToDeviceReadByTileRdBytes) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 4 * 1024;
    constexpr uint64_t addr = 0x0;

    auto write_data = make_pattern(data_size, [](size_t i) { return i % 256; });

    tt_device->write_to_device(write_data.data(), core, addr, data_size);

    std::vector<uint8_t> read_data(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_data.data(), data_size);

    EXPECT_EQ(write_data, read_data);
}

// ---------------------------------------------------------------------------
// tile_wr_bytes (direct path) → read_from_device (TLB path)
// ---------------------------------------------------------------------------

// Write a pattern via tile_wr_bytes, read it back via read_from_device and compare.
TEST_F(TTSimDeviceIOFixture, TileWrBytesReadByReadFromDevice) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 1024;
    constexpr uint64_t addr = 0x300;

    auto write_data = make_pattern(data_size, [](size_t i) { return (i * 3 + 7) % 256; });

    tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, write_data.data(), data_size);

    std::vector<uint8_t> read_data(data_size, 0);
    tt_device->read_from_device(read_data.data(), core, addr, data_size);

    EXPECT_EQ(write_data, read_data);
}

// Write zeros via tile_wr_bytes then a pattern; confirm read_from_device sees each state.
TEST_F(TTSimDeviceIOFixture, TileWrBytesZeroThenPatternReadByReadFromDevice) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x400;

    std::vector<uint8_t> zeros(data_size, 0);
    auto pattern = make_pattern(data_size, [](size_t i) { return 255 - i % 256; });

    // First write zeros.
    tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, zeros.data(), data_size);

    std::vector<uint8_t> read_zeros(data_size, 0xFF);
    tt_device->read_from_device(read_zeros.data(), core, addr, data_size);
    EXPECT_EQ(zeros, read_zeros) << "read_from_device should see zeros after tile_wr_bytes wrote zeros";

    // Then write the pattern.
    tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, pattern.data(), data_size);

    std::vector<uint8_t> read_pattern(data_size, 0);
    tt_device->read_from_device(read_pattern.data(), core, addr, data_size);
    EXPECT_EQ(pattern, read_pattern) << "read_from_device should see pattern after tile_wr_bytes wrote pattern";
}

// Large write (4 KB) via tile_wr_bytes, read back via read_from_device.
TEST_F(TTSimDeviceIOFixture, LargeTileWrBytesReadByReadFromDevice) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 4 * 1024;
    constexpr uint64_t addr = 0x0;

    auto write_data = make_pattern(data_size, [](size_t i) { return 255 - i % 256; });

    tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, write_data.data(), data_size);

    std::vector<uint8_t> read_data(data_size, 0);
    tt_device->read_from_device(read_data.data(), core, addr, data_size);

    EXPECT_EQ(write_data, read_data);
}

// ---------------------------------------------------------------------------
// Cross-API consistency: both read paths must agree after a single write
// ---------------------------------------------------------------------------

// Write via TLB; both read_from_device and tile_rd_bytes must return the same data.
TEST_F(TTSimDeviceIOFixture, WriteToDeviceBothReadsConsistent) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x500;

    auto write_data = make_pattern(data_size, [](size_t i) { return i % 256; });

    tt_device->write_to_device(write_data.data(), core, addr, data_size);

    std::vector<uint8_t> tlb_read(data_size, 0);
    tt_device->read_from_device(tlb_read.data(), core, addr, data_size);

    std::vector<uint8_t> direct_read(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read.data(), data_size);

    EXPECT_EQ(write_data, tlb_read) << "read_from_device disagrees with write_to_device";
    EXPECT_EQ(write_data, direct_read) << "tile_rd_bytes disagrees with write_to_device";
}

// Write via tile_wr_bytes; both read_from_device and tile_rd_bytes must return the same data.
TEST_F(TTSimDeviceIOFixture, TileWrBytesBothReadsConsistent) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x600;

    auto write_data = make_pattern(data_size, [](size_t i) { return (i * 5 + 13) % 256; });

    tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, write_data.data(), data_size);

    std::vector<uint8_t> tlb_read(data_size, 0);
    tt_device->read_from_device(tlb_read.data(), core, addr, data_size);

    std::vector<uint8_t> direct_read(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read.data(), data_size);

    EXPECT_EQ(write_data, tlb_read) << "read_from_device disagrees with tile_wr_bytes";
    EXPECT_EQ(write_data, direct_read) << "tile_rd_bytes disagrees with tile_wr_bytes";
}

// ---------------------------------------------------------------------------
// Multiple cores
// ---------------------------------------------------------------------------

// Write to the first N TENSIX cores alternating between the two write APIs;
// verify that both read APIs agree with what was written on every core.
TEST_F(TTSimDeviceIOFixture, MultiCoreAlternatingAPIsConsistent) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);

    const size_t num_cores = std::min(tensix_cores.size(), size_t(4));
    constexpr size_t data_size = 128;
    constexpr uint64_t addr = 0x700;

    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc.translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED).to_pair();

        auto write_data = make_pattern(data_size, [i](size_t j) { return (i * 50 + j) % 256; });

        // Alternate write APIs across cores.
        if (i % 2 == 0) {
            tt_device->write_to_device(write_data.data(), core, addr, data_size);
        } else {
            tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, write_data.data(), data_size);
        }

        std::vector<uint8_t> tlb_read(data_size, 0);
        tt_device->read_from_device(tlb_read.data(), core, addr, data_size);

        std::vector<uint8_t> direct_read(data_size, 0);
        tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read.data(), data_size);

        EXPECT_EQ(write_data, tlb_read) << "read_from_device mismatch on core index " << i;
        EXPECT_EQ(write_data, direct_read) << "tile_rd_bytes mismatch on core index " << i;
    }
}

// Write distinct data to multiple cores via write_to_device, read back via
// tile_rd_bytes; confirm no data bleed between cores.
TEST_F(TTSimDeviceIOFixture, MultiCoreWriteToDeviceReadByTileRdBytesNoBleed) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);

    const size_t num_cores = std::min(tensix_cores.size(), size_t(4));
    constexpr size_t data_size = 64;
    constexpr uint64_t addr = 0x800;

    // Write distinct patterns to each core.
    std::vector<std::vector<uint8_t>> all_patterns(num_cores);
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc.translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED).to_pair();
        all_patterns[i] = make_pattern(data_size, [i](size_t j) { return (i * 31 + j) % 256; });
        tt_device->write_to_device(all_patterns[i].data(), core, addr, data_size);
    }

    // Read back and verify each core still holds its own pattern.
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc.translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED).to_pair();
        std::vector<uint8_t> read_data(data_size, 0);
        tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_data.data(), data_size);
        EXPECT_EQ(all_patterns[i], read_data) << "Data bleed detected on core index " << i;
    }
}

// Write distinct data to multiple cores via tile_wr_bytes, read back via
// read_from_device; confirm no data bleed between cores.
TEST_F(TTSimDeviceIOFixture, MultiCoreTileWrBytesReadByReadFromDeviceNoBleed) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);

    const size_t num_cores = std::min(tensix_cores.size(), size_t(4));
    constexpr size_t data_size = 64;
    constexpr uint64_t addr = 0x900;

    // Write distinct patterns to each core.
    std::vector<std::vector<uint8_t>> all_patterns(num_cores);
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc.translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED).to_pair();
        all_patterns[i] = make_pattern(data_size, [i](size_t j) { return (i * 41 + j * 3) % 256; });
        tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, all_patterns[i].data(), data_size);
    }

    // Read back and verify each core still holds its own pattern.
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc.translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED).to_pair();
        std::vector<uint8_t> read_data(data_size, 0);
        tt_device->read_from_device(read_data.data(), core, addr, data_size);
        EXPECT_EQ(all_patterns[i], read_data) << "Data bleed detected on core index " << i;
    }
}

// ---------------------------------------------------------------------------
// Repeated write/read cycles (mirrors DynamicTLB_RW style)
// ---------------------------------------------------------------------------

// Repeatedly write a pattern then zeros via alternating APIs across several
// address offsets, confirming both read APIs agree at each step.
TEST_F(TTSimDeviceIOFixture, RepeatedWriteReadCycles) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t data_size = 40;
    constexpr uint32_t num_loops = 10;

    auto pattern = make_pattern(data_size, [](size_t i) { return i % 256; });
    std::vector<uint8_t> zeros(data_size, 0);

    uint64_t addr = 0x100;
    for (uint32_t loop = 0; loop < num_loops; loop++, addr += 0x40) {
        // Write pattern via TLB.
        tt_device->write_to_device(pattern.data(), core, addr, data_size);

        std::vector<uint8_t> tlb_read(data_size, 0);
        tt_device->read_from_device(tlb_read.data(), core, addr, data_size);
        EXPECT_EQ(pattern, tlb_read) << "TLB read mismatch at loop " << loop;

        std::vector<uint8_t> direct_read(data_size, 0);
        tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read.data(), data_size);
        EXPECT_EQ(pattern, direct_read) << "Direct read mismatch at loop " << loop;

        // Zero out via tile_wr_bytes.
        tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, zeros.data(), data_size);

        std::vector<uint8_t> tlb_read_zeros(data_size, 0xFF);
        tt_device->read_from_device(tlb_read_zeros.data(), core, addr, data_size);
        EXPECT_EQ(zeros, tlb_read_zeros) << "TLB read of zeros mismatch at loop " << loop;

        std::vector<uint8_t> direct_read_zeros(data_size, 0xFF);
        tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read_zeros.data(), data_size);
        EXPECT_EQ(zeros, direct_read_zeros) << "Direct read of zeros mismatch at loop " << loop;
    }
}

// ---------------------------------------------------------------------------
// 4GB TLB (BAR4) path — Blackhole only
// ---------------------------------------------------------------------------

// Allocate a 4GB TLB window explicitly. On Blackhole this is mapped through BAR4
// rather than BAR0, exercising SimulationTlbAllocator::get_tlb_address_from_index's
// BAR4 branch and the simulator's BAR4 TLB-translation path.
TEST_F(TTSimDeviceIOFixture, FourGBTlbBar4PathRoundTrip) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    if (soc.arch != tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "4GB TLBs only exist on Blackhole; skipping for arch " << tt::arch_to_str(soc.arch);
    }

    auto tensix_cores = soc.get_cores(CoreType::TENSIX);
    const tt_xy_pair core = soc.translate_coord_to(tensix_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t SIZE_4GB = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    auto tlb_window = tt_device->get_io_window({}, TlbMapping::WC, SIZE_4GB);
    ASSERT_NE(tlb_window, nullptr) << "Failed to allocate a 4GB TLB window on Blackhole";
    EXPECT_EQ(tlb_window->get_size(), SIZE_4GB);

    constexpr size_t data_size = 1024;
    constexpr uint64_t addr = 0x1000;
    auto write_data = make_pattern(data_size, [](size_t i) { return (i * 7 + 11) % 256; });

    // Write through the 4GB window (hits BAR4), read back through both the direct API and the
    // same 4GB window — all three views must agree.
    tlb_window->write_block_reconfigure(write_data.data(), core, addr, data_size, NocId::NOC0);

    std::vector<uint8_t> direct_read(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read.data(), data_size);
    EXPECT_EQ(write_data, direct_read) << "tile_rd_bytes disagrees with 4GB-TLB write";

    std::vector<uint8_t> tlb_read(data_size, 0);
    tlb_window->read_block_reconfigure(tlb_read.data(), core, addr, data_size, NocId::NOC0);
    EXPECT_EQ(write_data, tlb_read) << "4GB-TLB read disagrees with 4GB-TLB write";
}

// Same BAR4 path, but targeting a DRAM tile. The 4GB TLB resolves to a NOC (x,y) address
// independent of tile type, so this confirms the BAR4 route reaches DRAM as well as Tensix.
TEST_F(TTSimDeviceIOFixture, FourGBTlbBar4PathDramRoundTrip) {
    const SocDescriptor& soc = tt_device->get_soc_descriptor();
    if (soc.arch != tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "4GB TLBs only exist on Blackhole; skipping for arch " << tt::arch_to_str(soc.arch);
    }

    auto dram_cores = soc.get_cores(CoreType::DRAM);
    ASSERT_FALSE(dram_cores.empty()) << "No DRAM cores in SOC descriptor";
    const tt_xy_pair core = soc.translate_coord_to(dram_cores.at(0), CoordSystem::TRANSLATED).to_pair();

    constexpr size_t SIZE_4GB = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    auto tlb_window = tt_device->get_io_window({}, TlbMapping::WC, SIZE_4GB);
    ASSERT_NE(tlb_window, nullptr) << "Failed to allocate a 4GB TLB window on Blackhole";
    EXPECT_EQ(tlb_window->get_size(), SIZE_4GB);

    constexpr size_t data_size = 1024;
    constexpr uint64_t addr = 0x1000;
    auto write_data = make_pattern(data_size, [](size_t i) { return (i * 13 + 5) % 256; });

    tlb_window->write_block_reconfigure(write_data.data(), core, addr, data_size, NocId::NOC0);

    std::vector<uint8_t> direct_read(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, direct_read.data(), data_size);
    EXPECT_EQ(write_data, direct_read) << "tile_rd_bytes disagrees with 4GB-TLB write to DRAM";

    std::vector<uint8_t> tlb_read(data_size, 0);
    tlb_window->read_block_reconfigure(tlb_read.data(), core, addr, data_size, NocId::NOC0);
    EXPECT_EQ(write_data, tlb_read) << "4GB-TLB read disagrees with 4GB-TLB write to DRAM";
}

}  // namespace tt::umd
