// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The chips of a SIMULATION Cluster on the simulator TT_UMD_SIMULATOR names are built as a silicon Cluster's are: the
// chips this process reaches directly are LocalChips and are driven like them (start and close, host memory, ARC
// messages, clock state). Chips reached over Ethernet are covered by
// TTSimDiscoveryTest.RemoteChipsAreBuiltAsRemoteChips, and a client's chips by
// SimulationServerDiscovery.EveryChipOfAClientClusterIsALocalChip.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>

#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/chip/local_chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/cluster_descriptor.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/simulation_device_factory.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/cluster_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/simulation_backend_type.hpp"
#include "tt-umd/utils/timeouts.hpp"

using namespace tt::umd;

class SimulatedClusterChips : public ::testing::Test {
protected:
    static constexpr uint64_t L1_ADDRESS = 0x10000;
    static constexpr uint32_t PATTERN = 0xC0DE5EED;

    // ARC messages TTSim models (specs/ttsim/pcie-model.md, ARC access): TEST is accepted without effect on Wormhole
    // and Blackhole, GET_AICLK answers 1000 on Wormhole only. Wormhole's firmware takes its codes with a common prefix.
    static constexpr uint32_t WORMHOLE_ARC_TEST =
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::TEST);
    static constexpr uint32_t BLACKHOLE_ARC_TEST = static_cast<uint32_t>(blackhole::arc_message_type::TEST);
    static constexpr uint32_t WORMHOLE_ARC_GET_AICLK =
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::GET_AICLK);

    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::exists(simulator_path)) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a simulator.";
        }
        simulator_ = simulator_path;
    }

    // Every chip of the simulator's topology, with one host memory channel on each MMIO chip.
    ClusterOptions cluster_options() const {
        ClusterOptions options;
        options.chip_type = ChipType::SIMULATION;
        options.simulator_directory = simulator_;
        options.num_host_mem_ch_per_mmio_device = 1;
        return options;
    }

    std::unique_ptr<Cluster> open_cluster() const { return std::make_unique<Cluster>(cluster_options()); }

    static ChipId first_mmio_chip(Cluster& cluster) { return *cluster.get_target_mmio_device_ids().begin(); }

    // TTSim models ARC firmware for Wormhole and Blackhole only.
    static bool is_ttsim_with_arc(Cluster& cluster) {
        TTDevice* device = cluster.get_tt_device(first_mmio_chip(cluster));
        const tt::ARCH arch = device->get_arch();
        return device->get_simulation_backend() == SimulationBackendType::TTSIM &&
               (arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::BLACKHOLE);
    }

    std::filesystem::path simulator_;
};

TEST_F(SimulatedClusterChips, MmioChipsAreLocalChips) {
    const std::unique_ptr<Cluster> cluster = open_cluster();

    for (const ChipId chip : cluster->get_target_mmio_device_ids()) {
        EXPECT_NE(dynamic_cast<LocalChip*>(cluster->get_chip(chip)), nullptr) << "chip " << chip;
    }
}

// A simulated chip is on no NUMA node (specs/base-api/tt-device.md, get_numa_node: -1 when not PCIe-connected).
TEST_F(SimulatedClusterChips, MmioChipIsOnNoNumaNode) {
    const std::unique_ptr<Cluster> cluster = open_cluster();

    EXPECT_EQ(cluster->get_chip(first_mmio_chip(*cluster))->get_numa_node(), -1);
}

// Closing a chip releases its host memory binding only: started again, the chip still reaches its host memory.
TEST_F(SimulatedClusterChips, HostMemoryIsReachableAfterCloseAndStartAgain) {
    const std::unique_ptr<Cluster> cluster = open_cluster();
    Chip* chip = cluster->get_chip(first_mmio_chip(*cluster));
    if (cluster->get_tt_device(first_mmio_chip(*cluster))->get_simulation_backend() != SimulationBackendType::TTSIM) {
        GTEST_SKIP() << "Only an in-process TTSim device brings host memory of its own.";
    }
    ASSERT_EQ(chip->get_num_host_channels(), 1u);

    cluster->start_device({});
    cluster->close_device();
    cluster->start_device({});
    chip->write_to_sysmem(/*channel=*/0, &PATTERN, /*sysmem_dest=*/0, sizeof(PATTERN));
    uint32_t read = 0;
    chip->read_from_sysmem(/*channel=*/0, &read, /*sysmem_src=*/0, sizeof(read));
    cluster->close_device();

    EXPECT_EQ(read, PATTERN);
}

TEST_F(SimulatedClusterChips, TTSimChipCompletesAnArcTestMessage) {
    const std::unique_ptr<Cluster> cluster = open_cluster();
    if (!is_ttsim_with_arc(*cluster)) {
        GTEST_SKIP() << "Only TTSim Wormhole and Blackhole images model ARC.";
    }
    const ChipId chip = first_mmio_chip(*cluster);
    const uint32_t code =
        cluster->get_tt_device(chip)->get_arch() == tt::ARCH::WORMHOLE_B0 ? WORMHOLE_ARC_TEST : BLACKHOLE_ARC_TEST;

    EXPECT_EQ(cluster->get_chip(chip)->arc_msg(code), 0);
}

TEST_F(SimulatedClusterChips, TTSimWormholeChipReportsA1000MHzAiclkOverArc) {
    const std::unique_ptr<Cluster> cluster = open_cluster();
    const ChipId chip = first_mmio_chip(*cluster);
    if (!is_ttsim_with_arc(*cluster) || cluster->get_tt_device(chip)->get_arch() != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "Only TTSim Wormhole answers GET_AICLK.";
    }

    uint32_t aiclk = 0;
    ASSERT_EQ(
        cluster->get_chip(chip)->arc_msg(
            WORMHOLE_ARC_GET_AICLK, true, {}, timeout::ARC_MESSAGE_TIMEOUT, /*return_3=*/&aiclk),
        0);
    EXPECT_EQ(aiclk, 1000u);
}

// wh_x2's chip 1 is reached over Ethernet, and so is its ARC.
TEST_F(SimulatedClusterChips, TTSimWormholeRemoteChipReportsA1000MHzAiclkOverArc) {
    const std::unique_ptr<Cluster> cluster = open_cluster();
    const std::set<ChipId> remote_chips = cluster->get_target_remote_device_ids();
    if (!is_ttsim_with_arc(*cluster) || remote_chips.empty() ||
        cluster->get_soc_descriptor(*remote_chips.begin()).arch != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "Only TTSim Wormhole images model a chip reached over Ethernet that answers GET_AICLK.";
    }
    const ChipId remote_chip = *remote_chips.begin();

    uint32_t aiclk = 0;
    ASSERT_EQ(
        cluster->get_chip(remote_chip)
            ->arc_msg(WORMHOLE_ARC_GET_AICLK, true, {}, timeout::ARC_MESSAGE_TIMEOUT, /*return_3=*/&aiclk),
        0);
    EXPECT_EQ(aiclk, 1000u);
}

// Every simulated chip takes clock state changes, whether or not its simulator models the ARC messages behind them.
TEST_F(SimulatedClusterChips, SimulatedClusterGoesBusyAndIdle) {
    const std::unique_ptr<Cluster> cluster = open_cluster();

    EXPECT_NO_THROW(cluster->set_clock_state(DevicePowerState::BUSY));
    EXPECT_NO_THROW(cluster->set_clock_state(DevicePowerState::LONG_IDLE));
}

// Deprecated, but tt-metal still hands a SIMULATION Cluster a mock descriptor of one chip of the simulator's
// architecture, together with the SoC descriptor the simulator ships, whatever the simulator.
TEST_F(SimulatedClusterChips, CallerMockDescriptorChipIsAWorkingLocalChip) {
    tt::ARCH arch = tt::ARCH::Invalid;
    bool noc_translation_enabled = false;
    {
        // The simulator's own topology, opened only to tell what it simulates: one simulator per process at a time.
        const std::unique_ptr<Cluster> discovered = open_cluster();
        const ChipId chip = first_mmio_chip(*discovered);
        arch = discovered->get_cluster_description()->get_arch(chip);
        noc_translation_enabled = discovered->get_cluster_description()->get_noc_translation_table_en().at(chip);
    }
    const std::unique_ptr<ClusterDescriptor> mock =
        ClusterDescriptor::create_mock_cluster({0}, arch, noc_translation_enabled);
    ClusterOptions options = cluster_options();
    options.cluster_descriptor = mock.get();
    options.topology_discovery_options.soc_descriptor_path = find_simulator_soc_descriptor_path(simulator_);
    const std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options);
    ASSERT_NE(dynamic_cast<LocalChip*>(cluster->get_chip(0)), nullptr);

    const CoreCoord core = cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX).front();
    cluster->write_to_device(&PATTERN, sizeof(PATTERN), 0, core, L1_ADDRESS);
    uint32_t read = 0;
    cluster->read_from_device(&read, 0, core, L1_ADDRESS, sizeof(read));

    EXPECT_EQ(read, PATTERN);
}

// A device that brings host memory of its own keeps it: the channel count given to LocalChip::create is for a PCIe
// device that brings none.
TEST_F(SimulatedClusterChips, LocalChipKeepsTheHostMemoryItsDeviceBrings) {
    if (simulator_.extension() != ".so") {
        GTEST_SKIP() << "Only an in-process TTSim device brings host memory of its own.";
    }
    std::unique_ptr<TTDevice> device = create_simulation_tt_device(simulator_, /*num_host_mem_channels=*/1);
    device->init_tt_device();

    const std::unique_ptr<LocalChip> chip = LocalChip::create(std::move(device), /*num_host_mem_channels=*/2);

    EXPECT_EQ(chip->get_num_host_channels(), 1u);
}
