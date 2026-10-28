// SPDX-FileCopyrightText: (c) 2026 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include "tt-umd/cluster_descriptor.hpp"

namespace tt::umd {

struct ClusterDescriptorTestAccess {
    static void set_board(ClusterDescriptor &d, ChipId id, BoardType type) { d.chip_board_type[id] = type; }

    static void add(
        ClusterDescriptor &d, int id, const std::string &bdf, std::optional<uint16_t> bus, const std::string &err) {
        d.add_unopened_device(id, bdf, bus, err);
    }
};

TEST(UnopenedDeviceTray, UbbBlackholeBusGivesTray) {
    ClusterDescriptor d;
    ClusterDescriptorTestAccess::set_board(d, 0, BoardType::UBB_BLACKHOLE);
    ClusterDescriptorTestAccess::add(d, 5, "0000:47:00.0", 0x47, "open failed");
    const auto &u = d.get_unopened_devices().at(5);
    EXPECT_EQ(u.pci_bdf, "0000:47:00.0");
    EXPECT_EQ(u.tray_id, std::optional<uint8_t>(2));
    EXPECT_EQ(u.error, "open failed");
}

TEST(UnopenedDeviceTray, NoUbbChipGivesNoTray) {
    ClusterDescriptor d;
    ClusterDescriptorTestAccess::add(d, 5, "0000:47:00.0", 0x47, "e");
    EXPECT_EQ(d.get_unopened_devices().at(5).tray_id, std::nullopt);
    EXPECT_EQ(d.get_unopened_devices().at(5).pci_bdf, "0000:47:00.0");
}

TEST(UnopenedDeviceTray, NoBusGivesNoTray) {
    ClusterDescriptor d;
    ClusterDescriptorTestAccess::set_board(d, 0, BoardType::UBB_BLACKHOLE);
    ClusterDescriptorTestAccess::add(d, 5, "0000:47:00.0", std::nullopt, "e");
    EXPECT_EQ(d.get_unopened_devices().at(5).tray_id, std::nullopt);
    EXPECT_EQ(d.get_unopened_devices().at(5).error, "e");
}

}  // namespace tt::umd
