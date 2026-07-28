// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_types.hpp"
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

// discover() decides the role from what simulator_directory points at (a .so file hosts TTSim, a
// directory of sockets is a client, any other directory hosts RTL). A path that is neither a .so
// file nor a directory is unrecognized and must fail loudly rather than silently mis-hosting. No
// simulator needed -- this exercises the path classifier's rejection branch.
TEST(SimulationConnector, ThrowsOnUnrecognizedSimulatorPath) {
    SimulationConnectorOptions options;
    options.simulator_directory = "/nonexistent/neither-a-so-nor-a-directory";
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

// The host serves its full device identity over the socket; a client reads it back and it matches
// the host's live SoC descriptor field for field (serialization/deserialization through the socket).
// Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, HostServesDeviceInfoOverSocket) {
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

    SimulationClient client(socket);
    client.attach();
    SimulationServerRequest info_request;
    info_request.command = SimulationServerCommand::GetDeviceInfo;
    const SimulationServerDeviceInfo info = decode_device_info(client.transact(encode(info_request)));

    EXPECT_EQ(info.status, 0);
    EXPECT_EQ(info.arch, static_cast<int32_t>(soc.arch));
    const bool is_ttsim = std::filesystem::path(simulator_path).extension() == ".so";
    EXPECT_EQ(info.backend_type, is_ttsim ? SimulationBackendType::TTSim : SimulationBackendType::Rtl);
    EXPECT_FALSE(info.soc_descriptor_yaml.empty());
    EXPECT_EQ(info.noc_translation_enabled, soc.noc_translation_enabled);
    EXPECT_EQ(info.tensix_harvesting_mask, soc.harvesting_masks.tensix_harvesting_mask);
    EXPECT_EQ(info.dram_harvesting_mask, soc.harvesting_masks.dram_harvesting_mask);
    EXPECT_EQ(info.eth_harvesting_mask, soc.harvesting_masks.eth_harvesting_mask);
    EXPECT_EQ(info.l2cpu_harvesting_mask, soc.harvesting_masks.l2cpu_harvesting_mask);
    EXPECT_EQ(info.pcie_harvesting_mask, soc.harvesting_masks.pcie_harvesting_mask);
}

// End to end through the client device: hosting from the .so and then discovering the socket
// *directory* yields a client-mode device whose read_from_device/write_to_device marshal over the
// socket. What the client writes the host sees, and what the host writes the client reads back.
// Requires TT_UMD_SIMULATOR.
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

    SimulationConnectorOptions host_options;
    host_options.simulator_directory = simulator_path;

    // The .so path hosts and publishes its per-chip socket; pointing discovery at that socket's
    // directory takes the client path, attaching one client device per socket.
    auto host_devices = SimulationConnector::discover(host_options);
    ASSERT_EQ(host_devices.size(), 1u);
    TTDevice* host = host_devices.at(0).get();
    ASSERT_NE(host, nullptr);

    SimulationConnectorOptions client_options;
    client_options.simulator_directory = socket.parent_path();
    auto client_devices = SimulationConnector::discover(client_options);
    ASSERT_EQ(client_devices.count(0), 1u);
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

// End to end at the Cluster level: one Cluster hosts from the .so (serving a per-chip socket); a
// second Cluster pointed at the socket *directory* attaches as a client, reconstructs the topology
// over the wire, and cluster-level I/O crosses the socket. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, HostAndClientClustersShareDeviceMemory) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path socket0 = SimulationServerSocket::default_socket_path(0);
    {
        auto probe = SimulationServerSocket::try_create(socket0);
        if (probe == nullptr) {
            GTEST_SKIP() << "A live simulation host already holds " << socket0 << "; skipping.";
        }
    }

    ClusterOptions host_options;
    host_options.chip_type = ChipType::SIMULATION;
    host_options.simulator_directory = simulator_path;
    Cluster host_cluster(host_options);

    ClusterOptions client_options;
    client_options.chip_type = ChipType::SIMULATION;
    client_options.simulator_directory = socket0.parent_path();  // the socket directory => client role
    Cluster client_cluster(client_options);

    // The client reconstructed the same chips the host serves.
    EXPECT_EQ(client_cluster.get_target_device_ids(), host_cluster.get_target_device_ids());

    const tt::ChipId chip = 0;
    const SocDescriptor& soc = host_cluster.get_soc_descriptor(chip);
    const CoreCoord tensix = soc.get_cores(tt::CoreType::TENSIX).at(0);
    constexpr uint64_t addr = 0x1000;

    // Host writes through its Cluster; the client reads the same location back over the socket.
    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};
    host_cluster.write_to_device(pattern.data(), pattern.size(), chip, tensix, addr);
    std::vector<uint8_t> readback(pattern.size());
    client_cluster.read_from_device(readback.data(), chip, tensix, addr, pattern.size());
    EXPECT_EQ(readback, pattern);
}
