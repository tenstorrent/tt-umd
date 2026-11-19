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
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/tt_device/tt_device_factory.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/tlb.hpp"
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

TEST(PcieDeviceTest, MapPeerBar) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.size() < 2) {
        GTEST_SKIP() << "Mapping a peer BAR needs two devices.";
    }
    if (PCIDevice(device_ids[0]).get_arch() != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer BAR mapping is supported only on Blackhole.";
    }
    PCIDevice device(device_ids[0]);
    const PCIDevice peer(device_ids[1]);

    const uint64_t address = device.map_peer_bar(peer, 0, 0, getpagesize());
    EXPECT_NE(address, 0);
    EXPECT_EQ(address % getpagesize(), 0);
}

// A write sent to the mapped address reaches the core the peer's TLB window is aimed at. Blackhole's PCIe tile passes
// NOC addresses below 2^58 to PCIe unchanged, so the host sends that write through the device's PCIe tile without
// running anything on either chip.
TEST(PcieDeviceTest, MapPeerBarWriteReachesPeerWindow) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.size() < 2) {
        GTEST_SKIP() << "Mapping a peer BAR needs two devices.";
    }
    std::unique_ptr<TTDevice> device = create_tt_device(device_ids[0]);
    std::unique_ptr<TTDevice> peer = create_tt_device(device_ids[1]);
    if (device->get_arch() != ARCH::BLACKHOLE || peer->get_arch() != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer BAR mapping is supported only on Blackhole.";
    }
    for (TTDevice* tt_device : {device.get(), peer.get()}) {
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->init_tt_device();
    }

    const CoreCoord peer_core = peer->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    tlb_data config;
    config.x_end = peer_core.x;
    config.y_end = peer_core.y;
    config.ordering = tlb_data::Strict;
    SiliconTlbWindow peer_window(peer->get_pci_device()->allocate_tlb(blackhole::TLB_2M_SIZE), config);
    peer_window.write32(SAFE_IO_L1_ADDRESS, 0);

    const uint32_t bar_offset = blackhole::TLB_BASE_2M + peer_window.handle_ref().get_tlb_id() * blackhole::TLB_2M_SIZE;
    const uint64_t peer_window_address =
        device->get_pci_device()->map_peer_bar(*peer->get_pci_device(), 0, bar_offset, blackhole::TLB_2M_SIZE);

    const CoreCoord pcie_core = device->get_soc_descriptor().get_cores(CoreType::PCIE, CoordSystem::TRANSLATED)[0];
    const uint32_t pattern = 0x600d600d;
    device->write_to_device(&pattern, pcie_core, peer_window_address + SAFE_IO_L1_ADDRESS, sizeof(pattern));

    // The read follows the write along the same path, so it returns only once the write has landed.
    uint32_t read_through_device = 0;
    device->read_from_device(
        &read_through_device, pcie_core, peer_window_address + SAFE_IO_L1_ADDRESS, sizeof(pattern));
    EXPECT_EQ(read_through_device, pattern);
    EXPECT_EQ(peer_window.read32(SAFE_IO_L1_ADDRESS), pattern);
}

TEST(PcieDeviceTest, MapPeerBarRejectsSameDevice) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.empty()) {
        GTEST_SKIP() << "No Tenstorrent PCI devices found.";
    }
    if (PCIDevice(device_ids[0]).get_arch() != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer BAR mapping is supported only on Blackhole.";
    }
    PCIDevice device(device_ids[0]);

    EXPECT_THROW(device.map_peer_bar(device, 0, 0, getpagesize()), error::UmdException<error::RuntimeError>);
}

TEST(PcieDeviceTest, MapPeerBarThrowsOutsideBlackhole) {
    const std::vector<int> device_ids = PCIDevice::enumerate_devices();
    if (device_ids.size() < 2) {
        GTEST_SKIP() << "Mapping a peer BAR needs two devices.";
    }
    PCIDevice device(device_ids[0]);
    if (device.get_arch() == ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Peer BAR mapping is supported on Blackhole.";
    }
    const PCIDevice peer(device_ids[1]);

    EXPECT_THROW(device.map_peer_bar(peer, 0, 0, getpagesize()), error::UmdException<error::RuntimeError>);
}
