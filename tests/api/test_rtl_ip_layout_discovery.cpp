// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Topology discovery of an RTL simulator build with an ip_layout.yaml, with the simulator faked: the build is a
// scratch directory, and a thread per socket stands in for the simulator's side of it (dials the host, acks, answers
// reads), as docs/IP_LAYOUT.md describes for several processes sharing one run. No simulator is needed.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "simulation_device_generated.h"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/rtl_sim_mock_remote.hpp"
#include "tests/test_utils/simulation_discovery.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/topology/topology_discovery_options.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/core_coordinates.hpp"

using namespace tt::umd;
using tt::umd::test_utils::chips_of;
using tt::umd::test_utils::MockRemote;
using tt::umd::test_utils::simulation_discovery_options;
using tt::umd::test_utils::SimulatorLayout;

// A build whose layout has two devices on IP blocks of their own, each on its own socket and with its own SoC
// descriptor: device 0 is quasar_simulation_1x3.yaml (1 Tensix core), device 1 quasar_simulation_2x3.yaml (2 Tensix
// cores). The build's own soc_descriptor.yaml, quasar_simulation_9x4.yaml, has 32, so a device built from it rather
// than from the layout is caught. Each socket is pinned to a free port, so the fake simulator knows where to dial.
// run.sh only leaves a marker file, which shows whether this process launched the simulator. The variables the tests
// set, and a TT_VISIBLE_DEVICES that would pick the chips for them, are set aside and restored after each test.
class RtlIpLayoutDiscovery : public ::testing::Test {
protected:
    void SetUp() override {
        for (const char* variable : SAVED_VARIABLES) {
            const char* value = std::getenv(variable);
            saved_variables_.emplace_back(variable, value ? std::optional<std::string>(value) : std::nullopt);
        }
        unsetenv("TT_VISIBLE_DEVICES");
        layout_.add_soc_descriptor("quasar_simulation_9x4.yaml");
        for (const char* soc_descriptor : {"quasar_simulation_1x3.yaml", "quasar_simulation_2x3.yaml"}) {
            std::filesystem::copy_file(
                test_utils::get_soc_desc_path(soc_descriptor), layout_.directory() / soc_descriptor);
        }
        std::ofstream(layout_.directory() / "ip_layout.yaml") << R"(access_points:
  - host_access: umd_ip_layout_dev0
    ip_block: 0
  - host_access: umd_ip_layout_dev1
    ip_block: 1
devices:
  - access_points: [0]
    soc_descriptor: quasar_simulation_1x3.yaml
  - access_points: [1]
    soc_descriptor: quasar_simulation_2x3.yaml
)";
        const std::filesystem::path run_script = layout_.directory() / "run.sh";
        std::ofstream(run_script) << "#!/bin/sh\ntouch \"" << launch_marker().string() << "\"\n";
        std::filesystem::permissions(run_script, std::filesystem::perms::owner_all);

        setenv("TT_SIMULATOR_LOCALHOST", "1", 1);
        for (const std::string& socket : SOCKETS) {
            const std::string port = std::to_string(test_utils::free_local_port());
            setenv(("NNG_SOCKET_LOCAL_PORT_" + socket).c_str(), port.c_str(), 1);
            addresses_.push_back("tcp://localhost:" + port);
        }
    }

    void TearDown() override {
        serving_ = false;
        for (std::thread& simulator : simulators_) {
            simulator.join();
        }
        for (const std::string& socket : SOCKETS) {
            unsetenv(("NNG_SOCKET_LOCAL_PORT_" + socket).c_str());
            unsetenv(("NNG_SOCKET_ADDR_" + socket).c_str());
        }
        for (const auto& [variable, value] : saved_variables_) {
            if (value) {
                setenv(variable.c_str(), value->c_str(), 1);
            } else {
                unsetenv(variable.c_str());
            }
        }
    }

    // Plays the simulator's side of layout device @p device's socket until the test ends. Discovery waits for the
    // ack of every socket it serves, so a test that serves a socket the fake does not answer hangs.
    void simulate_device(int device) {
        simulators_.emplace_back([this, address = addresses_.at(device)] {
            MockRemote remote(address, DEVICE_COMMAND_EXIT);
            std::atomic<int> exits{0};
            while (serving_) {
                remote.answer_one(50, exits);
            }
        });
    }

    TopologyDiscoveryOptions served_by_another_process(const std::unordered_set<ChipId>& target_devices = {}) const {
        TopologyDiscoveryOptions options = simulation_discovery_options(layout_.directory(), target_devices);
        options.simulation->launch_simulator = false;
        return options;
    }

    // Whether run.sh has run within @p timeout. The simulator is launched as a separate process, so its marker can
    // appear only after discovery returns.
    bool launched_within(std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!std::filesystem::exists(launch_marker())) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    }

    std::filesystem::path launch_marker() const { return layout_.directory() / "launched"; }

    static constexpr const char* SAVED_VARIABLES[] = {
        "TT_VISIBLE_DEVICES", "TT_SIMULATOR_LOCALHOST", "TT_UMD_SIMULATOR_LAUNCH"};
    static inline const std::vector<std::string> SOCKETS = {"umd_ip_layout_dev0", "umd_ip_layout_dev1"};

    SimulatorLayout layout_;
    std::vector<std::string> addresses_;
    std::atomic<bool> serving_{true};
    std::vector<std::thread> simulators_;
    std::vector<std::pair<std::string, std::optional<std::string>>> saved_variables_;
};

TEST_F(RtlIpLayoutDiscovery, DiscoversOneChipPerLayoutDevice) {
    simulate_device(0);
    simulate_device(1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(served_by_another_process());

    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0, 1}));
    EXPECT_EQ(devices.at(0)->get_soc_descriptor().get_cores(CoreType::TENSIX).size(), 1u);
    EXPECT_EQ(devices.at(1)->get_soc_descriptor().get_cores(CoreType::TENSIX).size(), 2u);
}

TEST_F(RtlIpLayoutDiscovery, DoesNotLaunchWhenAnotherProcessDoes) {
    simulate_device(0);
    simulate_device(1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(served_by_another_process());

    EXPECT_FALSE(launched_within(std::chrono::seconds(2)));
}

// The positive control for DoesNotLaunchWhenAnotherProcessDoes: the marker does appear when this process launches.
TEST_F(RtlIpLayoutDiscovery, LaunchesTheSimulatorByDefault) {
    simulate_device(0);
    simulate_device(1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(layout_.directory()));

    EXPECT_TRUE(launched_within(std::chrono::seconds(2)));
}

// Only device 1's socket is answered: discovery must not serve device 0's, or it would wait for its ack.
TEST_F(RtlIpLayoutDiscovery, TargetedDeviceIsRenumberedFromZero) {
    simulate_device(1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(served_by_another_process({1}));

    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
    EXPECT_EQ(devices.at(0)->get_soc_descriptor().get_cores(CoreType::TENSIX).size(), 2u);
}

TEST_F(RtlIpLayoutDiscovery, DiscoveryStampsTheCallersClusterId) {
    simulate_device(0);
    simulate_device(1);
    TopologyDiscoveryOptions options = served_by_another_process();
    options.cluster_id = "ip-layout.cluster_1";

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(cluster_desc->get_cluster_id(), std::optional<std::string>("ip-layout.cluster_1"));
}

// No device is faked: discovery must refuse the id before it serves any socket.
TEST_F(RtlIpLayoutDiscovery, IllegalClusterIdIsRefused) {
    TopologyDiscoveryOptions options = served_by_another_process();
    options.cluster_id = "not a cluster id!";

    EXPECT_ANY_THROW(TopologyDiscovery::discover(options));
}

TEST_F(RtlIpLayoutDiscovery, ClusterLeavesLaunchingToAnotherProcess) {
    simulate_device(0);
    simulate_device(1);
    setenv("TT_UMD_SIMULATOR_LAUNCH", "0", 1);
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = layout_.directory();

    Cluster cluster(options);

    EXPECT_EQ(cluster.get_target_device_ids(), (std::set<ChipId>{0, 1}));
    EXPECT_FALSE(launched_within(std::chrono::seconds(2)));
}

// An RTL simulator publishes no firmware telemetry, so its devices report no clock rather than failing to.
TEST_F(RtlIpLayoutDiscovery, DiscoveredDeviceReportsNoClock) {
    simulate_device(0);
    simulate_device(1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(served_by_another_process());

    EXPECT_EQ(devices.at(0)->get_clock(), 0u);
    EXPECT_EQ(devices.at(0)->get_firmware_info_provider()->get_clock_freq(), std::nullopt);
}

// The cross-process locks of a device are named after its communication id, so each device of a build needs an id of
// its own: neither the -1 of a device without a communication context nor the id of another device.
TEST_F(RtlIpLayoutDiscovery, DiscoveredDevicesHaveCommunicationIdsOfTheirOwn) {
    simulate_device(0);
    simulate_device(1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(served_by_another_process());

    EXPECT_NE(devices.at(0)->get_communication_device_id(), -1);
    EXPECT_NE(devices.at(1)->get_communication_device_id(), -1);
    EXPECT_NE(devices.at(0)->get_communication_device_id(), devices.at(1)->get_communication_device_id());
}
