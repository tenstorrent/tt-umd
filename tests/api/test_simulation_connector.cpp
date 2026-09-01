// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#include "simulation/simulation_server_socket.hpp"
#include "tests/test_utils/simulation_socket_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/simulation_client.hpp"
#include "umd/device/simulation/simulation_connector.hpp"
#include "umd/device/simulation/simulation_server_protocol.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt::umd;

// Integration: with no live host, discovery creates a host device that binds + exposes its
// socket, and tears it down with the device. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, CreatesHostDeviceAndExposesSocket) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    // Each server gets its own directory, so a fresh one never collides with a concurrent run.
    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();
    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(server_directory, 0);

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    options.serve_over_sockets = true;  // this test exercises the socket-serving host path
    options.server_directory = server_directory;

    {
        auto devices = SimulationConnector::discover(options).devices;
        ASSERT_EQ(devices.size(), 1u);
        ASSERT_NE(devices.at(0), nullptr);
        EXPECT_TRUE(std::filesystem::exists(socket));  // host exposed it
    }

    EXPECT_FALSE(std::filesystem::exists(socket));                  // torn down with the device
    EXPECT_FALSE(std::filesystem::is_directory(server_directory));  // and its now-empty directory removed
}

// With serving off (the default), discover() still creates a working host device, but it is private
// in-process: no socket is published, so nothing else can attach. This is the direct-use entry point
// into the TTDevice layer for callers that don't want cross-process IPC. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, CreatesPrivateHostDeviceWhenNotServing) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    // No server directory is claimed when serving is off; if the connector wrongly served, it would
    // land here, so assert this stays empty.
    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();
    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(server_directory, 0);

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    // serve_over_sockets defaults to false: a private in-process host, no socket published.
    options.server_directory = server_directory;

    auto devices = SimulationConnector::discover(options).devices;
    ASSERT_EQ(devices.size(), 1u);
    EXPECT_NE(devices.at(0), nullptr);              // a usable host device...
    EXPECT_FALSE(std::filesystem::exists(socket));  // ...that published no socket

    std::filesystem::remove(server_directory);
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

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();
    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(server_directory, 0);

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    options.serve_over_sockets = true;  // this test exercises the socket-serving host path
    options.server_directory = server_directory;
    auto devices = SimulationConnector::discover(options).devices;
    ASSERT_EQ(devices.size(), 1u);
    TTDevice* host = devices.at(0).get();
    ASSERT_NE(host, nullptr);

    const SocDescriptor& soc = host->get_soc_descriptor();
    const CoreCoord tensix = soc.get_cores(tt::CoreType::TENSIX).at(0);
    // Client-side translation: what the client puts on the wire, passed through the host as-is.
    const tt_xy_pair noc = soc.translate_chip_coord_to_translated(tensix, get_selected_noc_id());
    constexpr uint64_t addr = 0x1000;
    const std::vector<uint8_t> pattern = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};

    SimulationClient client(socket);
    client.attach();

    // Host writes directly; the client reads the same location over the socket.
    host->write_to_device(pattern.data(), tensix, addr, pattern.size());
    SimulationServerRequest read_req;
    read_req.command = SimulationServerCommand::READ;
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
    write_req.command = SimulationServerCommand::WRITE;
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

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();
    const std::filesystem::path socket = SimulationServerSocket::default_socket_path(server_directory, 0);

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    options.serve_over_sockets = true;  // this test exercises the socket-serving host path
    options.server_directory = server_directory;
    auto devices = SimulationConnector::discover(options).devices;
    ASSERT_EQ(devices.size(), 1u);
    TTDevice* host = devices.at(0).get();
    ASSERT_NE(host, nullptr);
    const SocDescriptor& soc = host->get_soc_descriptor();

    SimulationClient client(socket);
    client.attach();
    SimulationServerRequest info_request;
    info_request.command = SimulationServerCommand::GET_DEVICE_INFO;
    const SimulationServerDeviceInfo info = decode_device_info(client.transact(encode(info_request)));

    EXPECT_EQ(info.status, 0);
    EXPECT_EQ(info.arch, static_cast<int32_t>(soc.arch));
    const bool is_ttsim = std::filesystem::path(simulator_path).extension() == ".so";
    EXPECT_EQ(info.backend_type, is_ttsim ? SimulationBackendType::TTSIM : SimulationBackendType::RTL);
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

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();

    SimulationConnectorOptions host_options;
    host_options.simulator_directory = simulator_path;
    host_options.serve_over_sockets = true;  // this test exercises the socket-serving host path
    host_options.server_directory = server_directory;

    // The .so path hosts and publishes its per-chip socket; pointing discovery at that server's
    // directory takes the client path, attaching one client device per socket.
    auto host_devices = SimulationConnector::discover(host_options).devices;
    ASSERT_EQ(host_devices.size(), 1u);
    TTDevice* host = host_devices.at(0).get();
    ASSERT_NE(host, nullptr);

    SimulationConnectorOptions client_options;
    client_options.simulator_directory = server_directory;
    auto client_devices = SimulationConnector::discover(client_options).devices;
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

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();

    ClusterOptions host_options;
    host_options.chip_type = ChipType::SIMULATION;
    host_options.simulator_directory = simulator_path;
    // Publishing per-chip sockets is opt-in (off by default so ordinary in-process runs stay
    // private); this test is specifically the host/client-over-socket path, so it opts in.
    host_options.serve_simulation_devices_over_sockets = true;
    host_options.simulator_server_directory = server_directory;
    // A simulator that ships a cluster_descriptor.yaml is enumerated from it, and an empty
    // target_devices then means "every chip in it". Without one, Cluster falls back to a mock
    // descriptor built *from* target_devices -- so leaving it empty there yields a Cluster with zero
    // chips, which serves zero sockets and gives the client nothing to attach to. Name chip 0 in that
    // case, and only that case.
    if (!std::filesystem::exists(SimulationChip::get_cluster_descriptor_path_from_simulator_path(simulator_path))) {
        host_options.target_devices = {0};
    }
    Cluster host_cluster(host_options);

    ClusterOptions client_options;
    client_options.chip_type = ChipType::SIMULATION;
    client_options.simulator_directory = server_directory;  // the server directory => client role
    Cluster client_cluster(client_options);

    // The client reconstructed the same chips the host serves.
    EXPECT_EQ(client_cluster.get_target_device_ids(), host_cluster.get_target_device_ids());

    // Each Cluster can say what it is connected to: the host names the simulator it runs and the
    // directory it serves in; the client names the directory it attached to and the simulator the
    // host reported over the wire.
    const auto host_connection = host_cluster.get_simulation_connection();
    ASSERT_TRUE(host_connection.has_value());
    EXPECT_EQ(host_connection->role, SimulationConnector::Role::Host);
    EXPECT_EQ(host_connection->simulator, std::filesystem::path(simulator_path));
    EXPECT_EQ(host_connection->server_directory, server_directory);
    EXPECT_FALSE(host_connection->sockets.empty());

    const auto client_connection = client_cluster.get_simulation_connection();
    ASSERT_TRUE(client_connection.has_value());
    EXPECT_EQ(client_connection->role, SimulationConnector::Role::Client);
    EXPECT_EQ(client_connection->server_directory, server_directory);
    EXPECT_EQ(client_connection->simulator, std::filesystem::path(simulator_path));
    EXPECT_EQ(client_connection->backend, host_connection->backend);
    EXPECT_EQ(client_connection->arch, host_connection->arch);

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

// discover() reports the connection it opened, not just the devices: as a serving host, the
// simulator it runs, the backend and arch behind it, and the directory and per-chip sockets it
// serves on. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, ReportsServingHostConnection) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    options.serve_over_sockets = true;
    options.server_directory = server_directory;

    const SimulationConnector::Result result = SimulationConnector::discover(options);
    const SimulationConnector::Connection& connection = result.connection;

    EXPECT_EQ(connection.role, SimulationConnector::Role::Host);
    EXPECT_EQ(connection.simulator, std::filesystem::path(simulator_path));
    const bool is_ttsim = std::filesystem::path(simulator_path).extension() == ".so";
    EXPECT_EQ(connection.backend, is_ttsim ? SimulationBackendType::TTSIM : SimulationBackendType::RTL);
    EXPECT_EQ(connection.arch, result.devices.at(0)->get_soc_descriptor().arch);
    EXPECT_EQ(connection.server_directory, server_directory);
    EXPECT_EQ(connection.sockets.size(), result.devices.size());
    EXPECT_EQ(connection.sockets.at(0), SimulationServerSocket::default_socket_path(server_directory, 0));
}

// The point of reporting the connection on the host side: with server_directory left empty the
// connector allocates one internally, and the caller has no other way to learn which. Requires
// TT_UMD_SIMULATOR.
TEST(SimulationConnector, ReportsTheServerDirectoryItAllocated) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    options.serve_over_sockets = true;
    // server_directory deliberately left empty: the connector allocates one.

    const SimulationConnector::Result result = SimulationConnector::discover(options);

    ASSERT_FALSE(result.connection.server_directory.empty());
    EXPECT_TRUE(std::filesystem::is_directory(result.connection.server_directory));
    ASSERT_EQ(result.connection.sockets.count(0), 1u);
    EXPECT_EQ(result.connection.sockets.at(0).parent_path(), result.connection.server_directory);
    EXPECT_TRUE(std::filesystem::exists(result.connection.sockets.at(0)));
}

// A private in-process host still reports its role and simulator; an empty server_directory is how
// "not serving" reads. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, ReportsPrivateHostConnection) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    SimulationConnectorOptions options;
    options.simulator_directory = simulator_path;
    // serve_over_sockets defaults to false.

    const SimulationConnector::Result result = SimulationConnector::discover(options);

    EXPECT_EQ(result.connection.role, SimulationConnector::Role::Host);
    EXPECT_EQ(result.connection.simulator, std::filesystem::path(simulator_path));
    EXPECT_NE(result.connection.arch, tt::ARCH::Invalid);
    EXPECT_TRUE(result.connection.server_directory.empty());
    EXPECT_TRUE(result.connection.sockets.empty());
}

// A client reports the directory it attached to and the simulator the host runs -- the latter comes
// over the wire, since a client has no local simulator build to read. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, ReportsClientConnection) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();

    SimulationConnectorOptions host_options;
    host_options.simulator_directory = simulator_path;
    host_options.serve_over_sockets = true;
    host_options.server_directory = server_directory;
    const SimulationConnector::Result host = SimulationConnector::discover(host_options);

    SimulationConnectorOptions client_options;
    client_options.simulator_directory = server_directory;
    const SimulationConnector::Result client = SimulationConnector::discover(client_options);

    EXPECT_EQ(client.connection.role, SimulationConnector::Role::Client);
    EXPECT_EQ(client.connection.server_directory, server_directory);
    EXPECT_EQ(client.connection.sockets, host.connection.sockets);
    // Reported by the host over GET_DEVICE_INFO, and matching what the host itself says it runs.
    EXPECT_EQ(client.connection.simulator, host.connection.simulator);
    EXPECT_EQ(client.connection.backend, host.connection.backend);
    EXPECT_EQ(client.connection.arch, host.connection.arch);
}

// A socket file proves only that someone bound the path once. A directory holding nothing but
// sockets a crashed host left behind is not a server to attach to, and saying so beats falling
// through to hosting an RTL build out of a server directory. Needs no simulator.
TEST(SimulationConnector, ThrowsOnADirectoryOfOnlyStaleSockets) {
    const std::filesystem::path directory = SimulationServerSocket::allocate_server_directory();
    test_utils::leave_stale_socket(SimulationServerSocket::default_socket_path(directory, 0));

    // Classification is what rejects it, so both entry points do.
    EXPECT_THROW(SimulationConnector::role_for(directory), std::exception);

    SimulationConnectorOptions options;
    options.simulator_directory = directory;
    EXPECT_THROW(SimulationConnector::discover(options), std::exception);

    std::filesystem::remove_all(directory);
}

// One stale socket beside a live one must not drag the healthy chip out of the client's device
// list, nor make the topology probe pick the dead socket. Requires TT_UMD_SIMULATOR.
TEST(SimulationConnector, IgnoresAStaleSocketBesideALiveOne) {
    const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
    if (simulator_path == nullptr) {
        GTEST_SKIP() << "TT_UMD_SIMULATOR is not set.";
    }

    const std::filesystem::path server_directory = SimulationServerSocket::allocate_server_directory();

    SimulationConnectorOptions host_options;
    host_options.simulator_directory = simulator_path;
    host_options.serve_over_sockets = true;
    host_options.server_directory = server_directory;
    const SimulationConnector::Result host = SimulationConnector::discover(host_options);
    ASSERT_EQ(host.devices.size(), 1u);  // the live host serves chip 0

    // A second chip's socket, left behind by a host that is gone.
    test_utils::leave_stale_socket(SimulationServerSocket::default_socket_path(server_directory, 1));

    SimulationConnectorOptions client_options;
    client_options.simulator_directory = server_directory;
    const SimulationConnector::Result client = SimulationConnector::discover(client_options);

    EXPECT_EQ(client.connection.role, SimulationConnector::Role::Client);
    // Chip 1 is dropped at classification, so it reaches neither the devices nor the reported
    // sockets -- the client sees exactly the chips actually being served.
    EXPECT_EQ(client.devices.size(), 1u);
    EXPECT_EQ(client.devices.count(1), 0u);
    EXPECT_EQ(client.connection.sockets, host.connection.sockets);

    std::filesystem::remove_all(server_directory);
}
