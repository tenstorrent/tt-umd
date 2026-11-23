// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <fmt/ranges.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tests/test_utils/test_api_common.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/io_window_config.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt;
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

// A write sent to the returned address reaches the peer's core. Blackhole's PCIe tile passes NOC addresses below 2^58
// to PCIe unchanged, so the host sends that write through the device's PCIe tile without running anything on either
// chip.
TEST(PcieDeviceTest, MapPeerWindowWriteReachesPeer) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.size() < 2) {
        GTEST_SKIP() << "Mapping a peer window needs two devices.";
    }
    std::unique_ptr<TTDevice> device = create_tt_device(device_ids[0]);
    std::unique_ptr<TTDevice> peer = create_tt_device(device_ids[1]);
    if (device->get_arch() != ARCH::BLACKHOLE || peer->get_arch() != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer windows are supported only on Blackhole.";
    }
    for (TTDevice* tt_device : {device.get(), peer.get()}) {
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->init_tt_device();
    }

    const CoreCoord peer_core = peer->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    const uint32_t zero = 0;
    peer->write_to_device(&zero, peer_core, SAFE_IO_L1_ADDRESS, sizeof(zero));

    const uint64_t peer_address =
        device->map_peer_window(*peer, peer_core, SAFE_IO_L1_ADDRESS, getpagesize(), IoOrdering::STRICT);

    const CoreCoord pcie_core = device->get_soc_descriptor().get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
    const uint32_t pattern = 0x600d600d;
    device->write_to_device(&pattern, pcie_core, peer_address, sizeof(pattern));

    // The read follows the write along the same path, so it returns only once the write has landed.
    uint32_t read_through_device = 0;
    device->read_from_device(&read_through_device, pcie_core, peer_address, sizeof(read_through_device));
    EXPECT_EQ(read_through_device, pattern);
    uint32_t read_on_peer = 0;
    peer->read_from_device(&read_on_peer, peer_core, SAFE_IO_L1_ADDRESS, sizeof(read_on_peer));
    EXPECT_EQ(read_on_peer, pattern);
}

TEST(PcieDeviceTest, MapPeerWindowRejectsSameDevice) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    std::unique_ptr<TTDevice> device = create_tt_device(device_ids[0]);
    if (device->get_arch() != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer windows are supported only on Blackhole.";
    }
    const CoreCoord core = device->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    EXPECT_THROW(
        device->map_peer_window(*device, core, SAFE_IO_L1_ADDRESS, getpagesize()),
        error::UmdException<error::RuntimeError>);
}

TEST(PcieDeviceTest, MapPeerWindowThrowsOutsideBlackhole) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.size() < 2) {
        GTEST_SKIP() << "Mapping a peer window needs two devices.";
    }
    std::unique_ptr<TTDevice> device = create_tt_device(device_ids[0]);
    std::unique_ptr<TTDevice> peer = create_tt_device(device_ids[1]);
    if (peer->get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer windows are supported on Blackhole.";
    }
    const CoreCoord peer_core = peer->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    EXPECT_THROW(
        device->map_peer_window(*peer, peer_core, SAFE_IO_L1_ADDRESS, getpagesize()),
        error::UmdException<error::RuntimeError>);
}
