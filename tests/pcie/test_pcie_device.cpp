// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;

TEST(PcieDeviceTest, Numa) {
    std::vector<int> nodes;

    for (auto device_id : PCIDevice::enumerate_devices()) {
        PCIDevice device(device_id);
        nodes.push_back(device.get_numa_node());
    }

    // Acceptable outcomes:
    // 1. all of them are -1 (not a NUMA system)
    // 2. all of them are >= 0 (NUMA system)
    // 3. empty vector (no devices enumerated)

    if (!nodes.empty()) {
        bool all_negative_one = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node == -1; });
        bool all_non_negative = std::all_of(nodes.begin(), nodes.end(), [](int node) { return node >= 0; });

        EXPECT_TRUE(all_negative_one || all_non_negative)
            << "NUMA nodes should either all be -1 (non-NUMA system) or all be non-negative (NUMA system)"
            << " but got: " << fmt::format("{}", fmt::join(nodes, ", "));
    } else {
        SUCCEED() << "No PCIe devices were enumerated";
    }
}

TEST(PcieDeviceTest, MapPeerBar) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.size() < 2) {
        GTEST_SKIP() << "Mapping a peer BAR needs two devices.";
    }
    PCIDevice device(device_ids[0]);
    const PCIDevice peer(device_ids[1]);

    EXPECT_NE(device.map_peer_bar(peer, 0, 0, getpagesize()), 0);
}

TEST(PcieDeviceTest, MapPeerBarRejectsSameDevice) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    PCIDevice device(device_ids[0]);

    EXPECT_THROW(device.map_peer_bar(device, 0, 0, getpagesize()), error::UmdException<error::RuntimeError>);
}
