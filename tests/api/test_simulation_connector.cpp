// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt::umd;

// Integration: with no live host, discovery creates a host device that binds + exposes its
// socket, and tears it down with the device. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, CreatesHostDeviceAndExposesSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(0);
    // discover() resolves this well-known machine-wide path internally, so the test can't point
    // it at a private one. Rather than blindly unlink it (which would clobber a host from a
    // concurrent run), probe with try_create: a live owner -> skip; otherwise we hold a fresh or
    // reclaimed socket, released here so discover() can bind it itself.
    {
        auto probe = SimulationServerSocket::try_create(socket);
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

// When a live host already holds the socket, discovery takes the client/attach path. Here it must
// still surface an error rather than hosting, because the client device reads its SoC descriptor
// from the (invalid) simulator directory and fails. This exercises the host-vs-client arbiter's
// client branch without a simulator.
TEST(SimulationConnector, ThrowsWhenClientCreationFailsAgainstLiveHost) {
    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(0);
    auto host = SimulationServerSocket::try_create(socket);
    if (host == nullptr) {
        GTEST_SKIP() << "A live simulation host already holds " << socket << "; skipping.";
    }

    SimulationConnectorOptions options;
    options.simulator_directory = "unused";  // client creation reads the SoC descriptor from here and throws.
    EXPECT_THROW(SimulationConnector::discover(options), std::exception);
}

// End to end: the host device serves device-memory requests over its socket. A client (same
// process here) reads back what the host wrote, and a client write is visible to the host --
// exercising handle_request + the protocol + the transport against a real backend. Coordinates
// are translated client-side and passed through the host verbatim. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, HostServesClientMemoryOverSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(0);
    {
        auto probe = SimulationServerSocket::try_create(socket);
        if (probe == nullptr) {
            GTEST_SKIP() << "A live simulation host already holds " << socket << "; skipping.";
        }
    }

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    auto devices = SimulationConnector::discover(options);
    ASSERT_EQ(devices.size(), 1u);
    TTDevice* host = devices.at(0).get();
    ASSERT_NE(host, nullptr);

    const SocDescriptor& soc = host->get_soc_descriptor();
    const CoreCoord tensix = soc.get_cores(tt::CoreType::TENSIX).at(0);
    // Client-side translation: what the client puts on the wire, passed through the host as-is.
    const tt_xy_pair noc = soc.translate_chip_coord_to_translated(tensix);
    constexpr uint64_t addr = 0x1000;
    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};

    SimulationClient client(socket);
    client.attach();

    // Host writes directly; the client reads the same location over the socket.
    host->write_to_device(pattern.data(), tensix, addr, pattern.size());
    SimulationServerRequest read_req;
    read_req.command = SimulationServerCommand::Read;
    read_req.x = static_cast<uint32_t>(noc.x);
    read_req.y = static_cast<uint32_t>(noc.y);
    read_req.address = addr;
    read_req.size = static_cast<uint32_t>(pattern.size());
    const SimulationServerResponse read_resp = decode_response(client.transact(encode(read_req)));
    EXPECT_EQ(read_resp.status, 0);
    EXPECT_EQ(read_resp.data, pattern);

    // The client writes over the socket; the host sees it directly.
    const std::vector<uint8_t> pattern2 = {0x55, 0x66, 0x77, 0x88};
    constexpr uint64_t addr2 = 0x2000;
    SimulationServerRequest write_req;
    write_req.command = SimulationServerCommand::Write;
    write_req.x = static_cast<uint32_t>(noc.x);
    write_req.y = static_cast<uint32_t>(noc.y);
    write_req.address = addr2;
    write_req.size = static_cast<uint32_t>(pattern2.size());
    write_req.data = pattern2;
    const SimulationServerResponse write_resp = decode_response(client.transact(encode(write_req)));
    EXPECT_EQ(write_resp.status, 0);

    std::vector<uint8_t> readback(pattern2.size());
    host->read_from_device(readback.data(), tensix, addr2, pattern2.size());
    EXPECT_EQ(readback, pattern2);
}

// End to end through the client device: a second open() against a live host yields a client-mode
// device whose read_from_device/write_to_device marshal over the socket. What the client writes
// the host sees, and what the host writes the client reads back. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, ClientDeviceReadsAndWritesOverSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(0);
    {
        auto probe = SimulationServerSocket::try_create(socket);
        if (probe == nullptr) {
            GTEST_SKIP() << "A live simulation host already holds " << socket << "; skipping.";
        }
    }

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;

    // First open becomes the host; the second sees the live host and attaches as a client device.
    auto host_devices = SimulationConnector::discover(options);
    ASSERT_EQ(host_devices.size(), 1u);
    TTDevice* host = host_devices.at(0).get();
    ASSERT_NE(host, nullptr);

    auto client_devices = SimulationConnector::discover(options);
    ASSERT_EQ(client_devices.size(), 1u);
    TTDevice* client = client_devices.at(0).get();
    ASSERT_NE(client, nullptr);

    const CoreCoord tensix = host->get_soc_descriptor().get_cores(tt::CoreType::TENSIX).at(0);

    // Client writes over the socket; the host sees it on the real backend.
    const std::vector<uint8_t> from_client = {0xDE, 0xAD, 0xBE, 0xEF};
    constexpr uint64_t addr = 0x1000;
    client->write_to_device(from_client.data(), tensix, addr, from_client.size());
    std::vector<uint8_t> host_readback(from_client.size());
    host->read_from_device(host_readback.data(), tensix, addr, from_client.size());
    EXPECT_EQ(host_readback, from_client);

    // Host writes; the client reads it back over the socket.
    const std::vector<uint8_t> from_host = {0x55, 0x66, 0x77, 0x88, 0x99};
    constexpr uint64_t addr2 = 0x2000;
    host->write_to_device(from_host.data(), tensix, addr2, from_host.size());
    std::vector<uint8_t> client_readback(from_host.size());
    client->read_from_device(client_readback.data(), tensix, addr2, from_host.size());
    EXPECT_EQ(client_readback, from_host);
}
