// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Tests that verify write_to_device/read_from_device (TLB-based path) and
// tile_wr_bytes/tile_rd_bytes (direct simulator API) produce consistent results.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

class TTSimDeviceIOFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const char* simulator_path = getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr) {
            return;  // tt_device stays nullptr; individual tests will skip.
        }
        tt_device = TTSimTTDevice::create(simulator_path);
        tt_device->start_device();
    }

    static void TearDownTestSuite() {
        if (tt_device) {
            tt_device->close_device();
            tt_device.reset();
        }
    }

    void SetUp() override {
        if (!tt_device) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not set. Skipping TTSim device IO tests.";
        }
    }

    // Convenience: return the translated (physical NOC) coordinate of the
    // index-th TENSIX core.
    static tt_xy_pair translated_tensix(size_t index = 0) {
        const SocDescriptor* soc = tt_device->get_soc_descriptor();
        auto cores = soc->get_cores(CoreType::TENSIX);
        return soc->translate_coord_to(cores.at(index), CoordSystem::TRANSLATED);
    }

    static std::unique_ptr<TTSimTTDevice> tt_device;
};

std::unique_ptr<TTSimTTDevice> TTSimDeviceIOFixture::tt_device = nullptr;

// ---------------------------------------------------------------------------
// write_to_device (TLB path) → tile_rd_bytes (direct path)
// ---------------------------------------------------------------------------

// Write a pattern via the TLB path, read it back via tile_rd_bytes and compare.
TEST_F(TTSimDeviceIOFixture, WriteToDeviceReadByTileRdBytes) {
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 1024;
    constexpr uint64_t addr = 0x100;

    std::vector<uint8_t> write_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        write_data[i] = static_cast<uint8_t>(i % 256);
    }

    tt_device->write_to_device(write_data.data(), core, addr, data_size);

    std::vector<uint8_t> read_data(data_size, 0);
    tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_data.data(), data_size);

    EXPECT_EQ(write_data, read_data);
}

// Write zeros via TLB then a pattern via TLB; confirm tile_rd_bytes sees each state.
TEST_F(TTSimDeviceIOFixture, WriteToDeviceZeroThenPatternReadByTileRdBytes) {
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x200;

    std::vector<uint8_t> zeros(data_size, 0);
    std::vector<uint8_t> pattern(data_size);
    for (size_t i = 0; i < data_size; i++) {
        pattern[i] = static_cast<uint8_t>(i);
    }

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
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 4 * 1024;
    constexpr uint64_t addr = 0x0;

    std::vector<uint8_t> write_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        write_data[i] = static_cast<uint8_t>(i % 256);
    }

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
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 1024;
    constexpr uint64_t addr = 0x300;

    std::vector<uint8_t> write_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        write_data[i] = static_cast<uint8_t>((i * 3 + 7) % 256);
    }

    tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, write_data.data(), data_size);

    std::vector<uint8_t> read_data(data_size, 0);
    tt_device->read_from_device(read_data.data(), core, addr, data_size);

    EXPECT_EQ(write_data, read_data);
}

// Write zeros via tile_wr_bytes then a pattern; confirm read_from_device sees each state.
TEST_F(TTSimDeviceIOFixture, TileWrBytesZeroThenPatternReadByReadFromDevice) {
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x400;

    std::vector<uint8_t> zeros(data_size, 0);
    std::vector<uint8_t> pattern(data_size);
    for (size_t i = 0; i < data_size; i++) {
        pattern[i] = static_cast<uint8_t>(255 - (i % 256));
    }

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
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 4 * 1024;
    constexpr uint64_t addr = 0x0;

    std::vector<uint8_t> write_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        write_data[i] = static_cast<uint8_t>(255 - (i % 256));
    }

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
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x500;

    std::vector<uint8_t> write_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        write_data[i] = static_cast<uint8_t>(i);
    }

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
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 256;
    constexpr uint64_t addr = 0x600;

    std::vector<uint8_t> write_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        write_data[i] = static_cast<uint8_t>((i * 5 + 13) % 256);
    }

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
    const SocDescriptor* soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc->get_cores(CoreType::TENSIX);

    const size_t num_cores = std::min(tensix_cores.size(), size_t(4));
    constexpr size_t data_size = 128;
    constexpr uint64_t addr = 0x700;

    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc->translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED);

        std::vector<uint8_t> write_data(data_size);
        for (size_t j = 0; j < data_size; j++) {
            write_data[j] = static_cast<uint8_t>((i * 50 + j) % 256);
        }

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
    const SocDescriptor* soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc->get_cores(CoreType::TENSIX);

    const size_t num_cores = std::min(tensix_cores.size(), size_t(4));
    constexpr size_t data_size = 64;
    constexpr uint64_t addr = 0x800;

    // Write distinct patterns to each core.
    std::vector<std::vector<uint8_t>> all_patterns(num_cores, std::vector<uint8_t>(data_size));
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc->translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED);
        for (size_t j = 0; j < data_size; j++) {
            all_patterns[i][j] = static_cast<uint8_t>((i * 31 + j) % 256);
        }
        tt_device->write_to_device(all_patterns[i].data(), core, addr, data_size);
    }

    // Read back and verify each core still holds its own pattern.
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc->translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED);
        std::vector<uint8_t> read_data(data_size, 0);
        tt_device->get_communicator()->tile_read_bytes(core.x, core.y, addr, read_data.data(), data_size);
        EXPECT_EQ(all_patterns[i], read_data) << "Data bleed detected on core index " << i;
    }
}

// Write distinct data to multiple cores via tile_wr_bytes, read back via
// read_from_device; confirm no data bleed between cores.
TEST_F(TTSimDeviceIOFixture, MultiCoreTileWrBytesReadByReadFromDeviceNoBleed) {
    const SocDescriptor* soc = tt_device->get_soc_descriptor();
    auto tensix_cores = soc->get_cores(CoreType::TENSIX);

    const size_t num_cores = std::min(tensix_cores.size(), size_t(4));
    constexpr size_t data_size = 64;
    constexpr uint64_t addr = 0x900;

    // Write distinct patterns to each core.
    std::vector<std::vector<uint8_t>> all_patterns(num_cores, std::vector<uint8_t>(data_size));
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc->translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED);
        for (size_t j = 0; j < data_size; j++) {
            all_patterns[i][j] = static_cast<uint8_t>((i * 41 + j * 3) % 256);
        }
        tt_device->get_communicator()->tile_write_bytes(core.x, core.y, addr, all_patterns[i].data(), data_size);
    }

    // Read back and verify each core still holds its own pattern.
    for (size_t i = 0; i < num_cores; i++) {
        const tt_xy_pair core = soc->translate_coord_to(tensix_cores[i], CoordSystem::TRANSLATED);
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
    const tt_xy_pair core = translated_tensix();

    constexpr size_t data_size = 40;
    constexpr uint32_t num_loops = 10;

    std::vector<uint8_t> pattern(data_size);
    for (size_t i = 0; i < data_size; i++) {
        pattern[i] = static_cast<uint8_t>(i % 256);
    }
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

}  // namespace tt::umd
