// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "simulation/simulation_socket.hpp"
#include "umd/device/simulation/simulation_topology_discovery.hpp"
#include "umd/device/tt_device/tt_device.hpp"

using namespace tt::umd;

// Integration: with no live host, discovery creates a host device that binds + exposes its
// socket, and tears it down with the device. Requires TT_UMD_SIMULATOR.
TEST(SimulationTopologyDiscovery, CreatesHostDeviceAndExposesSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationSocket::default_socket_path(0);
    std::filesystem::remove(socket);

    SimulationTopologyDiscoveryOptions options;
    options.simulator_directory = simulator_path;

    {
        auto devices = SimulationTopologyDiscovery::discover(options);
        ASSERT_EQ(devices.size(), 1u);
        ASSERT_NE(devices.at(0), nullptr);
        EXPECT_TRUE(std::filesystem::exists(socket));  // host exposed it
    }

    EXPECT_FALSE(std::filesystem::exists(socket));  // torn down with the device
}
