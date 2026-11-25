// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Topology discovery attaching to a simulation server: SimulationDiscoveryOptions::simulator_path names the server
// directory of a host that serves its chips over per-chip sockets. The host runs in this process on the TTSim image
// TT_UMD_SIMULATOR names: a Cluster serving with its deprecated serve flag, or (SimulationServerShippedSocDescriptor)
// the devices TopologyDiscovery opened, served with SimulationConnector::serve. SimulationServerDirectory needs no
// simulator.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "tests/test_utils/declared_topologies.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/simulation_discovery.hpp"
#include "tests/test_utils/simulation_socket_test_utils.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/simulation/simulation_client.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::chips_of;
using tt::umd::test_utils::DECLARED_BLACKHOLE_TWO_P150_BOARDS;
using tt::umd::test_utils::leave_stale_socket;
using tt::umd::test_utils::ScopedServerDirectory;
using tt::umd::test_utils::shipped_soc_descriptors;
using tt::umd::test_utils::ShippedSocDescriptorCase;
using tt::umd::test_utils::simulation_discovery_options;
using tt::umd::test_utils::SimulatorLayout;

namespace server_discovery_test {

constexpr uint64_t L1_ADDRESS = 0x1000;
const std::vector<uint8_t> PATTERN = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};

// Reads PATTERN's worth of bytes at L1_ADDRESS of core. read_from_device and write_to_device are TTDevice's
// counterparts of the base API's read_data and write_data.
std::vector<uint8_t> read_l1(TTDevice& device, CoreCoord core) {
    std::vector<uint8_t> data(PATTERN.size());
    device.read_from_device(data.data(), core, L1_ADDRESS, data.size());
    return data;
}

void write_l1(TTDevice& device, CoreCoord core, const std::vector<uint8_t>& data) {
    device.write_to_device(data.data(), core, L1_ADDRESS, data.size());
}

}  // namespace server_discovery_test

using namespace server_discovery_test;

// A Cluster serving a TTSim image over per-chip sockets in a server directory of its own.
class SimulationServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path)) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
        }
        image_ = simulator_path;
    }

    void serve(const std::filesystem::path& simulator, std::function<void()> shutdown_handler = {}) {
        ClusterOptions options;
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = simulator;
        options.serve_simulation_devices_over_sockets = true;
        options.simulator_server_directory = server_directory_.path();
        options.simulation_shutdown_handler = std::move(shutdown_handler);
        host_ = std::make_unique<Cluster>(options);
    }

    std::map<ChipId, std::filesystem::path> served_sockets() const {
        return host_->get_simulation_connection()->sockets;
    }

    // Leaves the socket of a served chip as a host that is gone leaves it: present, but nothing listens on it.
    void make_socket_stale(ChipId chip) const { leave_stale_socket(served_sockets().at(chip)); }

    // Whether every chip of the served topology has a socket.
    bool every_served_chip_has_a_socket() const {
        const auto sockets = served_sockets();
        const auto& chips = host_->get_cluster_description()->get_all_chips();
        return std::all_of(chips.begin(), chips.end(), [&sockets](ChipId chip) { return sockets.count(chip) != 0; });
    }

    // Whether the served topology has a chip the host reaches only over Ethernet.
    bool has_a_chip_without_mmio() const {
        const ClusterDescriptor* served = host_->get_cluster_description();
        return served->get_chips_with_mmio().size() < served->get_all_chips().size();
    }

    TopologyDiscoveryOptions client_options(const std::unordered_set<ChipId>& target_devices = {}) const {
        return simulation_discovery_options(server_directory_.path(), target_devices);
    }

    // A Cluster attached to the server as a client, of target_devices only when given.
    std::unique_ptr<Cluster> client_cluster(const std::unordered_set<ChipId>& target_devices = {}) const {
        ClusterOptions options;
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = server_directory_.path();
        options.target_devices = target_devices;
        return std::make_unique<Cluster>(options);
    }

    CoreCoord first_tensix(ChipId chip) const {
        return host_->get_soc_descriptor(chip).get_cores(CoreType::TENSIX).at(0);
    }

    std::filesystem::path image_;
    // Declared before the host so the host stops serving before its directory is removed.
    ScopedServerDirectory server_directory_{SimulationConnector::allocate_server_directory()};
    std::unique_ptr<Cluster> host_;
};

// The image as it ships, served whole: every chip of the served topology has a socket.
class SimulationServerDiscovery : public SimulationServerTest {
protected:
    void SetUp() override {
        SimulationServerTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        serve(image_);
    }
};

// The deprecated serve flag serves every chip of the cluster, including one the host reaches over Ethernet.
TEST_F(SimulationServerDiscovery, EveryServedChipHasASocket) { EXPECT_TRUE(every_served_chip_has_a_socket()); }

TEST_F(SimulationServerDiscovery, DiscoveryReturnsTheServedTopology) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    const ClusterDescriptor* served = host_->get_cluster_description();
    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), served->get_chip_unique_ids());
    EXPECT_EQ(cluster_desc->get_all_chips(), served->get_all_chips());
}

TEST_F(SimulationServerDiscovery, DiscoveryOpensADeviceForEveryServedChip) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    const auto served = host_->get_cluster_description()->get_all_chips();
    EXPECT_EQ(chips_of(devices), std::set<ChipId>(served.begin(), served.end()));
}

TEST_F(SimulationServerDiscovery, ClientDeviceReadsWhatTheServerWrote) {
    host_->write_to_device(PATTERN.data(), PATTERN.size(), 0, first_tensix(0), L1_ADDRESS);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    EXPECT_EQ(read_l1(*devices.at(0), first_tensix(0)), PATTERN);
}

TEST_F(SimulationServerDiscovery, ServerReadsWhatTheClientDeviceWrote) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    write_l1(*devices.at(0), first_tensix(0), PATTERN);

    std::vector<uint8_t> readback(PATTERN.size());
    host_->read_from_device(readback.data(), 0, first_tensix(0), L1_ADDRESS, readback.size());
    EXPECT_EQ(readback, PATTERN);
}

// The server owns the simulator, so a SoC descriptor of another architecture is not refused: it does not apply.
TEST_F(SimulationServerDiscovery, SocDescriptorPathDoesNotApply) {
    TopologyDiscoveryOptions options = client_options();
    options.soc_descriptor_path = host_->get_soc_descriptor(0).arch == tt::ARCH::WORMHOLE_B0
                                      ? test_utils::get_soc_desc_path("blackhole_140_arch.yaml")
                                      : test_utils::get_soc_desc_path("wormhole_b0_8x10.yaml");

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(devices.at(0)->get_soc_descriptor().arch, host_->get_soc_descriptor(0).arch);
}

// The server owns the devices' host memory, so a channel count of the client's own is not refused: it does not apply.
TEST_F(SimulationServerDiscovery, HostMemoryChannelsDoNotApply) {
    host_->write_to_device(PATTERN.data(), PATTERN.size(), 0, first_tensix(0), L1_ADDRESS);
    TopologyDiscoveryOptions options = client_options();
    options.simulation->num_host_mem_channels = 4;

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(read_l1(*devices.at(0), first_tensix(0)), PATTERN);
}

TEST_F(SimulationServerDiscovery, StaleSocketOfAnotherChipIsIgnored) {
    leave_stale_socket(server_directory_.path() / "tt-umd-sim-7.sock");

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    EXPECT_EQ(chips_of(devices).count(7), 0);
    EXPECT_EQ(devices.size(), served_sockets().size());
}

TEST_F(SimulationServerDiscovery, ServedChipWithoutALiveSocketIsRefused) {
    if (host_->get_cluster_description()->get_all_chips().size() < 2) {
        GTEST_SKIP() << "The served topology has one chip, whose socket is the only one.";
    }
    make_socket_stale(1);

    EXPECT_THROW(TopologyDiscovery::discover(client_options()), error::UmdException<error::RuntimeError>);
}

TEST_F(SimulationServerDiscovery, DiscoveryStampsTheCallersClusterId) {
    TopologyDiscoveryOptions options = client_options();
    options.cluster_id = "simulation-client.cluster_1";

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(cluster_desc->get_cluster_id(), std::optional<std::string>("simulation-client.cluster_1"));
}

TEST_F(SimulationServerDiscovery, IllegalClusterIdIsRefused) {
    TopologyDiscoveryOptions options = client_options();
    options.cluster_id = "not a cluster id!";

    EXPECT_ANY_THROW(TopologyDiscovery::discover(options));
}

TEST_F(SimulationServerDiscovery, UnknownTargetDeviceIsRefused) {
    EXPECT_THROW(TopologyDiscovery::discover(client_options({99})), error::UmdException<error::RuntimeError>);
}

TEST_F(SimulationServerDiscovery, ClusterAttachedToTheServerReportsAClientConnection) {
    const auto client = client_cluster();

    const auto connection = client->get_simulation_connection();

    ASSERT_TRUE(connection.has_value());
    EXPECT_EQ(connection->role, SimulationConnector::Role::Client);
    EXPECT_EQ(connection->server_directory, server_directory_.path());
    EXPECT_EQ(connection->sockets, served_sockets());
    EXPECT_EQ(connection->simulator, image_);
    EXPECT_EQ(connection->backend, SimulationBackendType::TTSIM);
    EXPECT_EQ(connection->arch, host_->get_soc_descriptor(0).arch);
}

TEST_F(SimulationServerDiscovery, ClientConnectionOutlivesTheServer) {
    const auto client = client_cluster();
    const std::map<ChipId, std::filesystem::path> sockets = served_sockets();
    const tt::ARCH arch = host_->get_soc_descriptor(0).arch;

    host_.reset();
    const auto connection = client->get_simulation_connection();

    ASSERT_TRUE(connection.has_value());
    EXPECT_EQ(connection->role, SimulationConnector::Role::Client);
    EXPECT_EQ(connection->server_directory, server_directory_.path());
    EXPECT_EQ(connection->sockets, sockets);
    EXPECT_EQ(connection->simulator, image_);
    EXPECT_EQ(connection->backend, SimulationBackendType::TTSIM);
    EXPECT_EQ(connection->arch, arch);
}

// The server's image declares the topology, so a client Cluster given one of its own is refused.
TEST_F(SimulationServerDiscovery, ClusterGivenAClusterDescriptorIsRefused) {
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = server_directory_.path();
    options.cluster_descriptor = host_->get_cluster_description();

    EXPECT_THROW(Cluster{options}, error::UmdException<error::RuntimeError>);
}

// The image served with a SoC descriptor shipped beside it whose cores differ from its architecture's built-in one. The
// host is TopologyDiscovery serving the devices it built from that descriptor.
class SimulationServerShippedSocDescriptor : public SimulationServerTest {
protected:
    void SetUp() override {
        SimulationServerTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        const tt::ARCH arch = get_simulator_arch(image_);
        if (shipped_soc_descriptors().count(arch) == 0) {
            GTEST_SKIP() << "No reduced SoC descriptor is declared for this architecture.";
        }
        shipped_ = shipped_soc_descriptors().at(arch);
        layout_.add_cluster_descriptor(shipped_.cluster_yaml);
        layout_.add_soc_descriptor(shipped_.soc_descriptor_name);
        std::tie(served_topology_, served_devices_) =
            TopologyDiscovery::discover(simulation_discovery_options(layout_.add_ttsim_library(image_)));
        std::map<ChipId, TTDevice*> devices;
        for (const auto& [chip, device] : served_devices_) {
            devices[chip] = device.get();
        }
        server_ = SimulationConnector::serve(devices, *served_topology_, server_directory_.path());
    }

    SimulatorLayout layout_;
    ShippedSocDescriptorCase shipped_{};
    std::unique_ptr<ClusterDescriptor> served_topology_;
    std::map<ChipId, std::unique_ptr<TTDevice>> served_devices_;
    // Declared after the devices so serving stops before they go.
    std::unique_ptr<SimulationConnector::Server> server_;
};

TEST_F(SimulationServerShippedSocDescriptor, ClientDeviceIsBuiltFromTheServersSocDescriptor) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    EXPECT_EQ(devices.at(0)->get_soc_descriptor().get_cores(shipped_.core_type).size(), shipped_.cores);
}

// The image served with a chip the host reaches only over Ethernet, as wh_x2's chip 1.
class SimulationServerNonMmioChip : public SimulationServerTest {
protected:
    void SetUp() override {
        SimulationServerTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        serve(image_);
        if (!has_a_chip_without_mmio()) {
            GTEST_SKIP() << "Every chip of the served topology has MMIO.";
        }
    }
};

// Without remote discovery the chip reached over Ethernet is left out, so the client needs no socket for it.
TEST_F(SimulationServerNonMmioChip, WithoutRemoteDiscoveryOnlyMmioChipsAreAttached) {
    TopologyDiscoveryOptions options = client_options();
    options.discover_remote_devices = false;

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(cluster_desc->get_all_chips(), (std::unordered_set<ChipId>{0}));
    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

// wh_x2's chip 1 is the chip without MMIO.
TEST_F(SimulationServerNonMmioChip, ClientReadsWhatTheHostWroteOnTheChipWithoutMmio) {
    host_->write_to_device(PATTERN.data(), PATTERN.size(), 1, first_tensix(1), L1_ADDRESS);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options());

    EXPECT_EQ(read_l1(*devices.at(1), first_tensix(1)), PATTERN);
}

// The bh_x2 image served with its chips declared as two single-chip boards, so a client can select one chip without
// its board mate.
class SimulationServerSingleChipBoards : public SimulationServerTest {
protected:
    void SetUp() override {
        SimulationServerTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        const char* expected_chips = std::getenv("TT_UMD_SIM_EXPECTED_CHIPS");
        if (expected_chips == nullptr || std::string(expected_chips) != "2" ||
            get_simulator_arch(image_) != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Only the bh_x2 image models two MMIO chips that can be declared as separate boards.";
        }
        layout_.add_cluster_descriptor(DECLARED_BLACKHOLE_TWO_P150_BOARDS);
        layout_.add_soc_descriptor("blackhole_140_arch.yaml");
        serve(layout_.add_ttsim_library(image_));
    }

    SimulatorLayout layout_;
};

TEST_F(SimulationServerSingleChipBoards, TargetedChipIsRenumberedFromZero) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options({1}));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550001}}));
    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

TEST_F(SimulationServerSingleChipBoards, TargetedChipsDeviceDrivesThatChip) {
    host_->write_to_device(PATTERN.data(), PATTERN.size(), 1, first_tensix(1), L1_ADDRESS);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options({1}));

    EXPECT_EQ(read_l1(*devices.at(0), first_tensix(1)), PATTERN);
}

TEST_F(SimulationServerSingleChipBoards, ClientClusterTargetingChipOneNumbersItZero) {
    const auto client = client_cluster({1});

    EXPECT_EQ(client->get_target_device_ids(), (std::set<ChipId>{0}));
    EXPECT_EQ(
        client->get_cluster_description()->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550001}}));
    EXPECT_EQ(
        client->get_simulation_connection()->sockets,
        (std::map<ChipId, std::filesystem::path>{{0, served_sockets().at(1)}}));
}

TEST_F(SimulationServerSingleChipBoards, ClientClusterTargetingChipOneDrivesThatChip) {
    host_->write_to_device(PATTERN.data(), PATTERN.size(), 1, first_tensix(1), L1_ADDRESS);
    const auto client = client_cluster({1});

    std::vector<uint8_t> readback(PATTERN.size());
    client->read_from_device(readback.data(), 0, first_tensix(1), L1_ADDRESS, readback.size());

    EXPECT_EQ(readback, PATTERN);
}

TEST_F(SimulationServerSingleChipBoards, OnlyTheSelectedChipNeedsALiveSocket) {
    make_socket_stale(0);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(client_options({1}));

    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

TEST_F(SimulationServerSingleChipBoards, SelectedChipWithoutALiveSocketIsRefused) {
    make_socket_stale(1);

    EXPECT_THROW(TopologyDiscovery::discover(client_options({1})), error::UmdException<error::RuntimeError>);
}

// A Cluster hosting the image itself, serving or not.
class SimulationServerHost : public SimulationServerTest {
protected:
    ClusterOptions host_options(bool serve) const {
        ClusterOptions options;
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = image_;
        options.serve_simulation_devices_over_sockets = serve;
        return options;
    }
};

TEST_F(SimulationServerHost, ClusterReportsTheServerDirectoryItAllocated) {
    // Declared before the host so the host stops serving before the directory it allocated is removed.
    std::optional<ScopedServerDirectory> allocated;
    const Cluster host(host_options(true));
    const auto connection = host.get_simulation_connection();
    ASSERT_TRUE(connection.has_value());
    allocated.emplace(connection->server_directory);

    EXPECT_EQ(connection->role, SimulationConnector::Role::Host);
    ASSERT_FALSE(connection->server_directory.empty());
    EXPECT_TRUE(std::filesystem::is_directory(connection->server_directory));
    EXPECT_EQ(connection->sockets.at(0).parent_path(), connection->server_directory);
}

TEST_F(SimulationServerHost, PrivateClusterReportsNoServerDirectory) {
    const Cluster host(host_options(false));

    const auto connection = host.get_simulation_connection();

    ASSERT_TRUE(connection.has_value());
    EXPECT_EQ(connection->role, SimulationConnector::Role::Host);
    EXPECT_EQ(connection->simulator, image_);
    EXPECT_EQ(connection->backend, SimulationBackendType::TTSIM);
    EXPECT_TRUE(connection->server_directory.empty());
    EXPECT_TRUE(connection->sockets.empty());
}

// The deprecated shutdown handler is what a served chip's SHUTDOWN request calls.
TEST_F(SimulationServerHost, ShutdownCallsTheClustersShutdownHandler) {
    std::atomic<int> shutdown_requests{0};
    serve(image_, [&shutdown_requests] { shutdown_requests++; });
    SimulationClient client(served_sockets().at(0));
    client.attach();
    SimulationServerRequest request;
    request.command = SimulationServerCommand::SHUTDOWN;

    const SimulationServerResponse response = decode_response(client.transact(encode(request)));

    EXPECT_EQ(response.status, 0);
    EXPECT_EQ(shutdown_requests.load(), 1);
}

TEST(SimulationServerDirectory, DirectoryOfOnlyStaleSocketsIsRefused) {
    const ScopedServerDirectory directory(SimulationConnector::allocate_server_directory());
    leave_stale_socket(directory.path() / "tt-umd-sim-0.sock");

    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(directory.path())),
        error::UmdException<error::RuntimeError>);
}
