// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "simulation/simulation_socket.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/tt_device/tt_device.hpp"

using namespace tt::umd;

// Integration: with no live host, discovery creates a host device that binds + exposes its
// socket, and tears it down with the device. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, CreatesHostDeviceAndExposesSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationSocket::default_socket_path(0);
    // discover() resolves this well-known machine-wide path internally, so the test can't point
    // it at a private one. Rather than blindly unlink it (which would clobber a host from a
    // concurrent run), probe with try_create: a live owner -> skip; otherwise we hold a fresh or
    // reclaimed socket, released here so discover() can bind it itself.
    {
        auto probe = SimulationSocket::try_create(socket);
        if (probe == nullptr) {
            GTEST_SKIP() << "A live simulation host already holds " << socket << "; skipping to avoid clobbering it.";
        }
    }

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;

    {
        auto devices = SimulationConnector::discover(options);
        ASSERT_EQ(devices.size(), 1u);
        ASSERT_NE(devices.at(0), nullptr);
        EXPECT_TRUE(std::filesystem::exists(socket));  // host exposed it
    }

    EXPECT_FALSE(std::filesystem::exists(socket));  // torn down with the device
}

// When a live host already holds the socket, discovery must report the not-yet-implemented
// client/attach path as an error rather than trying to host. This exercises the host-vs-client
// arbiter's throw branch without a simulator: discover() bails before TTSimTTDevice::create().
TEST(SimulationConnector, ThrowsWhenLiveHostAlreadyExists) {
    const std::filesystem::path socket = SimulationSocket::default_socket_path(0);
    auto host = SimulationSocket::try_create(socket);
    if (host == nullptr) {
        GTEST_SKIP() << "A live simulation host already holds " << socket << "; skipping.";
    }

    SimulationConnectorOptions options;
    options.simulator_directory = "unused";  // discover() throws before it is touched.
    EXPECT_THROW(SimulationConnector::discover(options), std::exception);
}
