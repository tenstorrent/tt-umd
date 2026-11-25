// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// SimulationConnector::serve: serving any TTDevice to other processes over per-chip sockets. The served devices are
// the chips of the default test cluster, so the same tests serve KMD-backed silicon (and the card a ttsim-qemu VM
// emulates) and in-process TTSim chips, remote chips included.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/simulation_discovery.hpp"
#include "tests/test_utils/simulation_socket_test_utils.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/simulation/simulation_client.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::chips_of;
using tt::umd::test_utils::ScopedServerDirectory;
using tt::umd::test_utils::simulation_discovery_options;

namespace serve_test {

constexpr uint64_t SERVED_L1_ADDRESS = 0x1000;
const std::vector<uint8_t> SERVED_PATTERN = {0xDE, 0xAD, 0xBE, 0xEF, 0x11, 0x22, 0x33, 0x44};

bool is_directory_empty(const std::filesystem::path& directory) {
    return std::filesystem::directory_iterator(directory) == std::filesystem::directory_iterator();
}

SimulationServerResponse send_shutdown(const std::filesystem::path& socket) {
    SimulationClient client(socket);
    client.attach();
    SimulationServerRequest request;
    request.command = SimulationServerCommand::SHUTDOWN;
    return decode_response(client.transact(encode(request)));
}

SimulationServerDeviceInfo request_device_info(SimulationClient& client) {
    SimulationServerRequest request;
    request.command = SimulationServerCommand::GET_DEVICE_INFO;
    return decode_device_info(client.transact(encode(request)));
}

SimulationServerDeviceInfo request_device_info(const std::filesystem::path& socket) {
    SimulationClient client(socket);
    client.attach();
    return request_device_info(client);
}

std::set<std::filesystem::path> directory_entries(const std::filesystem::path& directory) {
    std::set<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        entries.insert(entry.path());
    }
    return entries;
}

}  // namespace serve_test

using namespace serve_test;

// Serves the chips of the default test cluster in a server directory of its own.
class SimulationServerServing : public ::testing::Test {
protected:
    void SetUp() override {
        cluster_ = test_utils::make_default_test_cluster(ClusterOptions{}, /*needs_sysmem=*/true);
        for (ChipId chip : cluster_->get_target_device_ids()) {
            devices_[chip] = cluster_->get_tt_device(chip);
        }
        if (devices_.empty()) {
            GTEST_SKIP() << "No device to serve.";
        }
    }

    std::unique_ptr<SimulationConnector::Server> serve(
        const std::map<ChipId, TTDevice*>& devices, std::function<void()> shutdown_handler = {}) const {
        return serve(devices, directory_.path(), std::move(shutdown_handler));
    }

    std::unique_ptr<SimulationConnector::Server> serve(
        const std::map<ChipId, TTDevice*>& devices,
        const std::filesystem::path& directory,
        std::function<void()> shutdown_handler = {}) const {
        return SimulationConnector::serve(
            devices, *cluster_->get_cluster_description(), directory, std::move(shutdown_handler));
    }

    std::unique_ptr<SimulationConnector::Server> serve_all(std::function<void()> shutdown_handler = {}) const {
        return serve(devices_, std::move(shutdown_handler));
    }

    std::set<ChipId> served_chips() const { return cluster_->get_target_device_ids(); }

    CoreCoord first_tensix(ChipId chip) const {
        return cluster_->get_soc_descriptor(chip).get_cores(CoreType::TENSIX).at(0);
    }

    std::unique_ptr<Cluster> cluster_;
    std::map<ChipId, TTDevice*> devices_;
    ScopedServerDirectory directory_{SimulationConnector::allocate_server_directory()};
};

TEST_F(SimulationServerServing, ServesInTheDirectoryItIsGiven) {
    const auto server = serve_all();

    EXPECT_EQ(server->server_directory(), directory_.path());
}

TEST_F(SimulationServerServing, ServesASocketForEveryDevice) {
    const auto server = serve_all();

    std::set<ChipId> socket_chips;
    for (const auto& [chip, socket] : server->sockets()) {
        socket_chips.insert(chip);
        EXPECT_EQ(socket.parent_path(), directory_.path());
        EXPECT_TRUE(std::filesystem::exists(socket));
    }
    EXPECT_EQ(socket_chips, served_chips());
}

TEST_F(SimulationServerServing, ClaimsADirectoryWhenGivenNone) {
    std::optional<ScopedServerDirectory> claimed;
    const auto server = SimulationConnector::serve(devices_, *cluster_->get_cluster_description());
    claimed.emplace(server->server_directory());

    EXPECT_FALSE(server->server_directory().empty());
    EXPECT_NE(server->server_directory(), directory_.path());
    EXPECT_TRUE(std::filesystem::is_directory(server->server_directory()));
}

TEST_F(SimulationServerServing, ClientDiscoversTheServedTopology) {
    const auto server = serve_all();

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(directory_.path()));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), cluster_->get_cluster_description()->get_chip_unique_ids());
    EXPECT_EQ(chips_of(devices), served_chips());
}

TEST_F(SimulationServerServing, DeviceInfoNamesTheDevicesArchitecture) {
    const auto server = serve_all();
    const ChipId chip = devices_.begin()->first;

    const SimulationServerDeviceInfo info = request_device_info(server->sockets().at(chip));

    EXPECT_EQ(info.status, 0);
    EXPECT_EQ(info.arch, static_cast<int32_t>(cluster_->get_soc_descriptor(chip).arch));
}

TEST_F(SimulationServerServing, DeviceNoSimulatorBacksIsDescribedAsTTSimWithoutASimulator) {
    const ChipId chip = devices_.begin()->first;
    if (get_simulation_backend(*devices_.at(chip)).has_value()) {
        GTEST_SKIP() << "The served device is simulated.";
    }
    const auto server = serve_all();

    const SimulationServerDeviceInfo info = request_device_info(server->sockets().at(chip));

    EXPECT_EQ(info.backend_type, SimulationBackendType::TTSIM);
    EXPECT_EQ(info.simulator_path, "");
}

TEST_F(SimulationServerServing, TTSimDeviceIsDescribedWithItsSimulator) {
    const char* simulator = std::getenv("TT_UMD_SIMULATOR");
    if (simulator == nullptr || std::filesystem::path(simulator).extension() != ".so") {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
    }
    const auto server = serve_all();

    // Chip 0 is the chip the simulator runs directly.
    const SimulationServerDeviceInfo info = request_device_info(server->sockets().at(0));

    EXPECT_EQ(info.backend_type, SimulationBackendType::TTSIM);
    EXPECT_EQ(info.simulator_path, simulator);
}

TEST_F(SimulationServerServing, ChipReachedThroughAnotherIsDescribedWithoutASimulatorPath) {
    const char* simulator = std::getenv("TT_UMD_SIMULATOR");
    if (simulator == nullptr || std::filesystem::path(simulator).extension() != ".so") {
        GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
    }
    const auto mmio_chips = cluster_->get_cluster_description()->get_chips_with_mmio();
    const auto remote = std::find_if(devices_.begin(), devices_.end(), [&mmio_chips](const auto& entry) {
        return mmio_chips.count(entry.first) == 0;
    });
    if (remote == devices_.end()) {
        GTEST_SKIP() << "Every chip of the cluster has MMIO.";
    }
    const auto server = serve_all();

    const SimulationServerDeviceInfo info = request_device_info(server->sockets().at(remote->first));

    EXPECT_EQ(info.backend_type, SimulationBackendType::TTSIM);
    EXPECT_EQ(info.simulator_path, "");
}

TEST_F(SimulationServerServing, ShutdownCallsTheHandlerBeforeItIsAcknowledged) {
    std::atomic<int> shutdown_requests{0};
    const auto server = serve_all([&shutdown_requests] { shutdown_requests++; });

    const SimulationServerResponse response = send_shutdown(server->sockets().begin()->second);

    EXPECT_EQ(response.status, 0);
    EXPECT_EQ(shutdown_requests.load(), 1);
}

TEST_F(SimulationServerServing, ShutdownWithoutAHandlerIsOnlyAcknowledged) {
    const auto server = serve_all();
    const ChipId chip = devices_.begin()->first;
    devices_.at(chip)->write_to_device(
        SERVED_PATTERN.data(), first_tensix(chip), SERVED_L1_ADDRESS, SERVED_PATTERN.size());

    const SimulationServerResponse response = send_shutdown(server->sockets().at(chip));

    EXPECT_EQ(response.status, 0);
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(directory_.path()));
    std::vector<uint8_t> readback(SERVED_PATTERN.size());
    devices.at(chip)->read_from_device(readback.data(), first_tensix(chip), SERVED_L1_ADDRESS, readback.size());
    EXPECT_EQ(readback, SERVED_PATTERN);
}

TEST_F(SimulationServerServing, DestroyingTheServerRemovesItsSockets) {
    auto server = serve_all();
    const std::map<ChipId, std::filesystem::path> sockets = server->sockets();

    server.reset();

    for (const auto& [chip, socket] : sockets) {
        EXPECT_FALSE(std::filesystem::exists(socket)) << "chip " << chip;
    }
}

TEST_F(SimulationServerServing, DestroyingTheServerDisconnectsAttachedClients) {
    auto server = serve_all();
    SimulationClient client(server->sockets().begin()->second);
    client.attach();
    ASSERT_EQ(request_device_info(client).status, 0);

    server.reset();

    EXPECT_ANY_THROW(request_device_info(client));
}

TEST_F(SimulationServerServing, DestroyingTheServerRemovesTheClaimedDirectoryItLeftEmpty) {
    auto server = serve_all();

    server.reset();

    EXPECT_FALSE(std::filesystem::exists(directory_.path()));
}

TEST_F(SimulationServerServing, DestroyingTheServerRemovesTheDirectoryServeClaimed) {
    auto server = SimulationConnector::serve(devices_, *cluster_->get_cluster_description());
    const ScopedServerDirectory claimed(server->server_directory());

    server.reset();

    EXPECT_FALSE(std::filesystem::exists(claimed.path()));
}

TEST_F(SimulationServerServing, DestroyingTheServerKeepsAClaimedDirectoryWithOtherContent) {
    const std::filesystem::path other_file = directory_.path() / "not-a-socket.txt";
    std::ofstream(other_file) << "kept";
    auto server = serve_all();

    server.reset();

    EXPECT_TRUE(std::filesystem::exists(other_file));
}

TEST_F(SimulationServerServing, DestroyingTheServerKeepsADirectoryItDidNotClaim) {
    const ScopedServerDirectory own_directory(
        std::filesystem::temp_directory_path() / ("umd-serve-test-own-directory-" + std::to_string(getpid())));
    std::filesystem::create_directory(own_directory.path());
    auto server = serve(devices_, own_directory.path());

    server.reset();

    EXPECT_TRUE(std::filesystem::is_directory(own_directory.path()));
    EXPECT_TRUE(is_directory_empty(own_directory.path()));
}

// A chip of the served topology left out of the devices gets no socket, so a client of the whole topology refuses it.
TEST_F(SimulationServerServing, ChipLeftOutIsRefusedByAWholeTopologyClient) {
    if (devices_.size() < 2) {
        GTEST_SKIP() << "The cluster has one chip, which cannot be left out.";
    }
    const ChipId first = devices_.begin()->first;
    const auto server = serve({{first, devices_.at(first)}});

    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(directory_.path())),
        error::UmdException<error::RuntimeError>);
}

TEST_F(SimulationServerServing, ChipNotInTheTopologyIsRefused) {
    EXPECT_THROW(serve({{99, devices_.begin()->second}}), error::UmdException<error::RuntimeError>);
    EXPECT_TRUE(is_directory_empty(directory_.path()));
}

TEST_F(SimulationServerServing, NullDeviceIsRefusedAndNothingIsServed) {
    const ChipId first = devices_.begin()->first;
    const auto topology = ClusterDescriptor::create_mock_cluster(
        {first, first + 1}, cluster_->get_soc_descriptor(first).arch, /*noc_translation_enabled=*/false);

    EXPECT_THROW(
        SimulationConnector::serve({{first, devices_.at(first)}, {first + 1, nullptr}}, *topology, directory_.path()),
        error::UmdException<error::RuntimeError>);
    EXPECT_TRUE(is_directory_empty(directory_.path()));
}

TEST_F(SimulationServerServing, ChipServedLiveInTheDirectoryIsRefused) {
    const auto server = serve_all();

    EXPECT_THROW(serve_all(), error::UmdException<error::RuntimeError>);
    for (const auto& [chip, socket] : server->sockets()) {
        EXPECT_TRUE(std::filesystem::exists(socket)) << "chip " << chip;
    }
}

// A later chip refused after an earlier one was bound leaves only what the first server serves.
TEST_F(SimulationServerServing, ChipServedLiveAfterAnotherWasBoundIsRefused) {
    if (devices_.size() < 2) {
        GTEST_SKIP() << "The cluster has one chip, so no chip is bound before the refused one.";
    }
    const ChipId last = devices_.rbegin()->first;
    const auto first_server = serve({{last, devices_.at(last)}});

    EXPECT_THROW(serve_all(), error::UmdException<error::RuntimeError>);
    EXPECT_EQ(
        directory_entries(directory_.path()), (std::set<std::filesystem::path>{first_server->sockets().at(last)}));
}

TEST(SimulationServerServingWithoutDevices, EmptyDeviceMapIsRefused) {
    const ScopedServerDirectory directory(SimulationConnector::allocate_server_directory());
    const auto topology =
        ClusterDescriptor::create_mock_cluster({0}, tt::ARCH::WORMHOLE_B0, /*noc_translation_enabled=*/false);

    EXPECT_THROW(SimulationConnector::serve({}, *topology, directory.path()), error::UmdException<error::RuntimeError>);
    EXPECT_TRUE(is_directory_empty(directory.path()));
}

// Client I/O on one served chip: 0 is local everywhere; 1 is wh_x2's and n300's remote chip and bh_x2's second chip.
class SimulationServerServingChip : public SimulationServerServing, public ::testing::WithParamInterface<ChipId> {
protected:
    void SetUp() override {
        SimulationServerServing::SetUp();
        if (IsSkipped()) {
            return;
        }
        if (devices_.count(GetParam()) == 0) {
            GTEST_SKIP() << "The cluster has no chip " << GetParam() << ".";
        }
        server_ = serve_all();
    }

    std::unique_ptr<SimulationConnector::Server> server_;
};

TEST_P(SimulationServerServingChip, ClientReadsWhatTheServedDeviceWrote) {
    const ChipId chip = GetParam();
    devices_.at(chip)->write_to_device(
        SERVED_PATTERN.data(), first_tensix(chip), SERVED_L1_ADDRESS, SERVED_PATTERN.size());

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(directory_.path()));
    std::vector<uint8_t> readback(SERVED_PATTERN.size());
    devices.at(chip)->read_from_device(readback.data(), first_tensix(chip), SERVED_L1_ADDRESS, readback.size());

    EXPECT_EQ(readback, SERVED_PATTERN);
}

TEST_P(SimulationServerServingChip, ServedDeviceReadsWhatTheClientWrote) {
    const ChipId chip = GetParam();
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(directory_.path()));

    devices.at(chip)->write_to_device(
        SERVED_PATTERN.data(), first_tensix(chip), SERVED_L1_ADDRESS, SERVED_PATTERN.size());

    std::vector<uint8_t> readback(SERVED_PATTERN.size());
    devices_.at(chip)->read_from_device(readback.data(), first_tensix(chip), SERVED_L1_ADDRESS, readback.size());
    EXPECT_EQ(readback, SERVED_PATTERN);
}

// Prefixed so the full names start with SimulationServer, which CI's SimulationServer*.* filters select.
INSTANTIATE_TEST_SUITE_P(
    SimulationServer,
    SimulationServerServingChip,
    ::testing::Values(0, 1),
    [](const ::testing::TestParamInfo<ChipId>& info) { return "Chip" + std::to_string(info.param); });
