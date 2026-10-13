// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// A partitioned RTL simulator build (one with an ip_layout.yaml) opened as one chip per device.
// Runs when TT_UMD_SIMULATOR points at such a build; skipped otherwise. One Cluster, and so one
// simulator run, serves the whole suite. The chips are the layout's devices, or the subset named by
// TT_VISIBLE_DEVICES, so the suite also runs as one of several processes sharing the run.
//
// The HorizonAtt tests use the noc2axi bridge ATT of Horizon: its registers sit behind the noc2axi
// APB port, reached at (1 << 56) | apb address through the device's tensix (0,0). They skip when the
// ATT is off.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

#include "simulation/rtl_sim_ip_layout.hpp"
#include "tt-umd/cluster.hpp"
#include "tt-umd/simulation/rtl_sim_communicator.hpp"
#include "tt-umd/topology/ip_layout.hpp"
#include "tt-umd/tt_device/rtl_simulation_tt_device.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/types/risc_type.hpp"

using namespace tt::umd;

namespace {

constexpr uint64_t L1_ADDR = 0x50000;
constexpr uint64_t APB_PORT = 1ULL << 56;
constexpr uint64_t ATT_ENABLE = 0x0201'0000;
constexpr uint64_t ATT_MASK_ENTRY_0 = 0x0201'0030;
constexpr uint64_t ATT_EP_TABLE_BASE = 0x0201'2000;
constexpr uint32_t ATT_EP_STRIDE = 4;
// Endpoint id (y << 6) | x of virtual (2,0): outside a device's grid, so nothing routes through it.
constexpr uint32_t UNUSED_EP_ID = 2;
// Horizon's TSC_RESET_CTRL, 8 bytes, and its uncore_reset_n (1 = released).
constexpr uint64_t TSC_RESET_CTRL = 0x0303'3088;
constexpr uint32_t TSC_DM_UNCORE_BIT = 24;

std::vector<uint32_t> marker(uint32_t tag) {
    std::vector<uint32_t> data(16);
    for (uint32_t i = 0; i < data.size(); i++) {
        data[i] = tag | i;
    }
    return data;
}

class RtlSimIpPartitionTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const char* simulator = std::getenv("TT_UMD_SIMULATOR");
        if (simulator == nullptr ||
            !std::filesystem::exists(std::filesystem::path(simulator) / RtlSimIpLayout::FILE_NAME)) {
            return;
        }
        layout_ = std::make_unique<RtlSimIpLayout>(simulator);

        ClusterOptions options;
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = simulator;
        options.num_host_mem_ch_per_mmio_device = 0;
        cluster_ = std::make_unique<Cluster>(options);
        cluster_->start_device({.init_device = true});
    }

    static void TearDownTestSuite() {
        cluster_.reset();
        layout_.reset();
    }

    void SetUp() override {
        if (cluster_ == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not an RTL simulator build with an ip_layout.yaml.";
        }
    }

    static CoreCoord tensix(tt::ChipId chip) {
        return cluster_->get_soc_descriptor(chip).get_cores(tt::CoreType::TENSIX)[0];
    }

    static uint32_t read32(tt::ChipId chip, uint64_t addr) {
        uint32_t value = 0;
        cluster_->read_from_device(&value, chip, tensix(chip), addr, sizeof(value));
        return value;
    }

    static void write32(tt::ChipId chip, uint64_t addr, uint32_t value) {
        cluster_->write_to_device(&value, sizeof(value), chip, tensix(chip), addr);
    }

    static bool att_enabled(tt::ChipId chip) { return (read32(chip, APB_PORT | ATT_ENABLE) & 1) != 0; }

    // TT_UMD_NOC_ATT switches UMD to flat addresses: every access is resolved through that map's
    // windows. The bridge ATT's own registers sit behind the APB port (bit 56), outside every window,
    // so the tests that read them run only in coordinate mode, without TT_UMD_NOC_ATT.
    static bool bridge_registers_reachable() { return std::getenv("TT_UMD_NOC_ATT") == nullptr; }

    static uint32_t table_offset(tt::ChipId chip) { return (read32(chip, APB_PORT | ATT_MASK_ENTRY_0) >> 18) & 0x3FF; }

    static bool dm_uncore_released(tt::ChipId chip) {
        uint64_t value = 0;
        cluster_->read_from_device(&value, chip, tensix(chip), TSC_RESET_CTRL, sizeof(value));
        return ((value >> TSC_DM_UNCORE_BIT) & 1) != 0;
    }

    static inline std::unique_ptr<RtlSimIpLayout> layout_;
    static inline std::unique_ptr<Cluster> cluster_;
};

TEST_F(RtlSimIpPartitionTest, OneChipPerDevice) {
    size_t expected = layout_->get_devices().size();
    if (const char* visible = std::getenv("TT_VISIBLE_DEVICES"); visible != nullptr && *visible != '\0') {
        expected = std::count(visible, visible + std::strlen(visible), ',') + 1;
    }
    EXPECT_EQ(cluster_->get_target_device_ids().size(), expected);
}

TEST_F(RtlSimIpPartitionTest, DevicesAreIsolated) {
    const std::set<tt::ChipId> chips = cluster_->get_target_device_ids();
    const size_t bytes = marker(0).size() * sizeof(uint32_t);

    for (tt::ChipId chip : chips) {
        const auto data = marker(0xA0000000u | (static_cast<uint32_t>(chip) << 20));
        cluster_->write_to_device(data.data(), bytes, chip, tensix(chip), L1_ADDR);
    }

    // Each device serves its commands in order, so reading every device once guarantees all writes
    // have landed before the second, checked, pass.
    std::vector<uint32_t> readback(marker(0).size());
    for (tt::ChipId chip : chips) {
        cluster_->read_from_device(readback.data(), chip, tensix(chip), L1_ADDR, bytes);
    }
    for (tt::ChipId chip : chips) {
        cluster_->read_from_device(readback.data(), chip, tensix(chip), L1_ADDR, bytes);
        EXPECT_EQ(readback, marker(0xA0000000u | (static_cast<uint32_t>(chip) << 20))) << "chip " << chip;
    }
}

TEST_F(RtlSimIpPartitionTest, HorizonAttEachDeviceHasItsOwnWindow) {
    if (!bridge_registers_reachable()) {
        GTEST_SKIP() << "Reads the bridge ATT registers, which flat addresses (TT_UMD_NOC_ATT) cannot reach.";
    }
    std::unordered_set<uint32_t> offsets;
    for (tt::ChipId chip : cluster_->get_target_device_ids()) {
        if (!att_enabled(chip)) {
            GTEST_SKIP() << "ATT is off on chip " << chip << ".";
        }
        offsets.insert(table_offset(chip));
        std::cout << "chip " << chip << ": bridge ATT table offset " << table_offset(chip) << std::endl;
    }
    EXPECT_EQ(offsets.size(), cluster_->get_target_device_ids().size());
}

TEST_F(RtlSimIpPartitionTest, HorizonAttReadModifyWriteBack) {
    if (!bridge_registers_reachable()) {
        GTEST_SKIP() << "Reads the bridge ATT registers, which flat addresses (TT_UMD_NOC_ATT) cannot reach.";
    }
    const size_t bytes = marker(0).size() * sizeof(uint32_t);
    for (tt::ChipId chip : cluster_->get_target_device_ids()) {
        if (!att_enabled(chip)) {
            GTEST_SKIP() << "ATT is off on chip " << chip << ".";
        }
        const uint64_t slot = APB_PORT | (ATT_EP_TABLE_BASE + (table_offset(chip) + UNUSED_EP_ID) * ATT_EP_STRIDE);
        const uint32_t original = read32(chip, slot);
        const uint32_t changed = original ^ 0xA5A;

        write32(chip, slot, changed);
        EXPECT_EQ(read32(chip, slot), changed) << "chip " << chip;

        const auto data = marker(0xB0000000u | (static_cast<uint32_t>(chip) << 20));
        std::vector<uint32_t> readback(data.size());
        cluster_->write_to_device(data.data(), bytes, chip, tensix(chip), L1_ADDR);
        cluster_->read_from_device(readback.data(), chip, tensix(chip), L1_ADDR, bytes);
        EXPECT_EQ(readback, data) << "chip " << chip << ": tensix does not resolve while the ATT is modified";
        EXPECT_EQ(read32(chip, slot), changed) << "chip " << chip;

        write32(chip, slot, original);
        EXPECT_EQ(read32(chip, slot), original) << "chip " << chip;
    }
}

// Each of a device's access points has its own flat address of a tile, and all of them reach that
// tile: a marker written through one access point reads back through every other. Skips without flat
// addresses (TT_UMD_NOC_ATT) or a device with two access points.
TEST_F(RtlSimIpPartitionTest, AccessPointsReachTheSameTile) {
    const size_t bytes = marker(0).size() * sizeof(uint32_t);
    bool checked = false;
    for (tt::ChipId chip : cluster_->get_target_device_ids()) {
        auto* device = dynamic_cast<RtlSimulationTTDevice*>(cluster_->get_tt_device(chip));
        ASSERT_NE(device, nullptr);
        std::vector<uint64_t> addresses;
        for (const AccessPointId access_point : device->get_access_points()) {
            const std::optional<uint64_t> address = device->get_flat_address(
                access_point, tensix(chip), WindowFlags::Bidirectional, IoOrdering::Strict, L1_ADDR);
            if (address.has_value()) {
                addresses.push_back(*address);
            }
        }
        if (addresses.size() < 2) {
            continue;
        }
        checked = true;
        for (size_t writer = 0; writer < addresses.size(); writer++) {
            const auto data = marker(0xC0000000u | (static_cast<uint32_t>(chip) << 20) | (writer << 16));
            device->get_communicator()->global_write_words(addresses[writer], data.data(), bytes);
            for (size_t reader = 0; reader < addresses.size(); reader++) {
                std::vector<uint32_t> readback(data.size());
                device->get_communicator()->global_read_words(addresses[reader], readback.data(), bytes);
                EXPECT_EQ(readback, data) << "chip " << chip << ": written through access point " << writer << " at 0x"
                                          << std::hex << addresses[writer] << ", read through " << std::dec << reader
                                          << " at 0x" << std::hex << addresses[reader];
            }
        }
    }
    if (!checked) {
        GTEST_SKIP() << "No device has two access points with flat addresses.";
    }
}

// On Horizon (the SoC descriptor's ip_variant), UMD resets a core by writing its TSC itself, so a
// reset reaches that core of that device only, whichever socket the device talks over. It holds and
// releases the DM uncore, which runs nothing while the DM cores stay held.
TEST_F(RtlSimIpPartitionTest, ResetsReachOnlyTheirDevice) {
    const std::set<tt::ChipId> chips = cluster_->get_target_device_ids();
    for (tt::ChipId chip : chips) {
        if (cluster_->get_soc_descriptor(chip).get_arch_descriptor().get_ip_variant() != tt::IpVariant::HORIZON) {
            GTEST_SKIP() << "Chip " << chip << "'s SoC descriptor names no Horizon ip_variant.";
        }
    }
    for (tt::ChipId chip : chips) {
        cluster_->get_tt_device(chip)->deassert_risc_reset(tensix(chip), RiscType::NEO_DM_UNCORE, false);
    }

    for (tt::ChipId target : chips) {
        cluster_->get_tt_device(target)->assert_risc_reset(tensix(target), RiscType::NEO_DM_UNCORE);
        for (tt::ChipId chip : chips) {
            EXPECT_EQ(dm_uncore_released(chip), chip != target)
                << "chip " << chip << "'s DM uncore, with chip " << target << "'s held";
        }
        cluster_->get_tt_device(target)->deassert_risc_reset(tensix(target), RiscType::NEO_DM_UNCORE, false);
        EXPECT_TRUE(dm_uncore_released(target)) << "chip " << target;
    }
}

}  // namespace
