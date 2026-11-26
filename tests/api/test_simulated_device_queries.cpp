// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// What a simulated device answers to the queries a chip makes of any TTDevice: its clock, its firmware info provider
// and the communication id its cross-process locks are named after. SimulatedDeviceQueries asks the devices discovery
// opens on the simulator TT_UMD_SIMULATOR names (a TTSim image, or an RTL simulator build);
// SimulationServerClientQueries asks the devices a client attaches to when that TTSim image is served over sockets.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>

#include "tests/test_utils/simulation_discovery.hpp"
#include "tests/test_utils/simulation_socket_test_utils.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/simulation/simulation_connector.hpp"
#include "tt-umd/topology/topology_discovery.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/simulation_backend_type.hpp"

using namespace tt::umd;
using tt::umd::test_utils::ScopedServerDirectory;
using tt::umd::test_utils::simulation_discovery_options;

namespace {

// Whether UMD reads firmware telemetry from the simulator backing device: TTSim models it for Wormhole and Blackhole,
// while Quasar TTSim and RTL devices report no AICLK. Selects which clock test applies to the simulator under test.
bool reports_firmware_telemetry(const TTDevice& device) {
    const tt::ARCH arch = device.get_soc_descriptor().arch;
    return device.get_simulation_backend() == SimulationBackendType::TTSIM &&
           (arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE);
}

}  // namespace

// The devices discovery opens on TT_UMD_SIMULATOR, opened once for the suite: bringing an RTL simulator up per test
// would take most of the run.
class SimulatedDeviceQueries : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::exists(simulator_path)) {
            return;
        }
        devices_ = TopologyDiscovery::discover(simulation_discovery_options(simulator_path)).second;
    }

    static void TearDownTestSuite() { devices_.clear(); }

    void SetUp() override {
        if (devices_.empty()) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a simulator.";
        }
    }

    // A device this process reaches directly, not over Ethernet.
    static TTDevice& gateway() {
        for (const auto& [chip, device] : devices_) {
            if (!device->is_remote()) {
                return *device;
            }
        }
        throw std::logic_error("Discovery opened no device this process reaches directly.");
    }

    // A device reached over Ethernet through a gateway, or nullptr when the simulator models none.
    static TTDevice* remote_device() {
        for (const auto& [chip, device] : devices_) {
            if (device->is_remote()) {
                return device.get();
            }
        }
        return nullptr;
    }

    static inline std::map<ChipId, std::unique_ptr<TTDevice>> devices_;
};

// TTSim publishes a fixed 1000 MHz AICLK in the Wormhole and Blackhole telemetry it models
// (specs/ttsim/device-model.md, telemetry tag 14).
TEST_F(SimulatedDeviceQueries, TTSimWormholeOrBlackholeGatewayReportsA1000MHzClock) {
    if (!reports_firmware_telemetry(gateway())) {
        GTEST_SKIP() << "Only TTSim Wormhole and Blackhole images publish a clock.";
    }

    EXPECT_EQ(gateway().get_clock(), 1000u);
}

TEST_F(SimulatedDeviceQueries, GatewayWithoutFirmwareTelemetryReportsNoClock) {
    if (reports_firmware_telemetry(gateway())) {
        GTEST_SKIP() << "The simulator publishes firmware telemetry.";
    }

    EXPECT_EQ(gateway().get_clock(), 0u);
}

// Only TTSim Wormhole images model a chip reached over Ethernet, and their telemetry carries the 1000 MHz clock.
TEST_F(SimulatedDeviceQueries, RemoteDeviceReportsA1000MHzClock) {
    TTDevice* remote = remote_device();
    if (remote == nullptr) {
        GTEST_SKIP() << "The simulator models no chip reached over Ethernet.";
    }

    EXPECT_EQ(remote->get_clock(), 1000u);
}

// The firmware info provider is a component every device serves, whether or not its simulator models firmware.
TEST_F(SimulatedDeviceQueries, GatewayServesAFirmwareInfoProvider) {
    EXPECT_NE(gateway().get_firmware_info_provider(), nullptr);
}

// A provider answers std::nullopt for a value the device does not report (specs/base-api/firmware.md).
TEST_F(SimulatedDeviceQueries, GatewayWithoutFirmwareTelemetryHasNoClockFrequency) {
    if (reports_firmware_telemetry(gateway())) {
        GTEST_SKIP() << "The simulator publishes firmware telemetry.";
    }

    EXPECT_EQ(gateway().get_firmware_info_provider()->get_clock_freq(), std::nullopt);
}

// A device whose simulator this process drives answers an id unique to this process rather than the -1 of a device with
// no communication context.
TEST_F(SimulatedDeviceQueries, GatewayHasACommunicationIdOfItsOwn) {
    EXPECT_NE(gateway().get_communication_device_id(), -1);
}

TEST_F(SimulatedDeviceQueries, RemoteDeviceHasACommunicationId) {
    TTDevice* remote = remote_device();
    if (remote == nullptr) {
        GTEST_SKIP() << "The simulator models no chip reached over Ethernet.";
    }

    EXPECT_NE(remote->get_communication_device_id(), -1);
}

// The TTSim image TT_UMD_SIMULATOR names, served over sockets by a host in this process, and the devices a client
// attaches to through discovery, opened once for the suite.
class SimulationServerClientQueries : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path)) {
            return;
        }
        directory_ = std::make_unique<ScopedServerDirectory>(SimulationConnector::allocate_server_directory());
        SimulationConnectorOptions host_options;
        host_options.simulator_directory = simulator_path;
        host_options.serve_over_sockets = true;
        host_options.server_directory = directory_->path();
        host_ = std::make_unique<SimulationConnector::Result>(SimulationConnector::discover(host_options));
        client_devices_ = TopologyDiscovery::discover(simulation_discovery_options(directory_->path())).second;
    }

    // Clients detach before the host stops serving, and the host stops before its directory goes.
    static void TearDownTestSuite() {
        client_devices_.clear();
        host_.reset();
        directory_.reset();
    }

    void SetUp() override {
        if (client_devices_.empty()) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
        }
    }

    static TTDevice& client() { return *client_devices_.begin()->second; }

    static inline std::unique_ptr<ScopedServerDirectory> directory_;
    static inline std::unique_ptr<SimulationConnector::Result> host_;
    static inline std::map<ChipId, std::unique_ptr<TTDevice>> client_devices_;
};

// A client reaches the device through the host's socket, which carries no firmware telemetry.
TEST_F(SimulationServerClientQueries, ClientReportsNoClock) { EXPECT_EQ(client().get_clock(), 0u); }

TEST_F(SimulationServerClientQueries, ClientHasNoClockFrequency) {
    EXPECT_EQ(client().get_firmware_info_provider()->get_clock_freq(), std::nullopt);
}

TEST_F(SimulationServerClientQueries, ClientHasNoCommunicationIdOfItsOwn) {
    EXPECT_EQ(client().get_communication_device_id(), -1);
}
