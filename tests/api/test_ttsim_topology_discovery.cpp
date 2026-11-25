// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Topology discovery of a TTSim image: a cluster_descriptor.yaml beside the image declares the topology, and without
// one discovery enumerates the image's endpoints and walks its Ethernet links. Both need TT_UMD_SIMULATOR naming a
// Wormhole or Blackhole TTSim library and TT_UMD_SIM_EXPECTED_CHIPS naming how many chips it models, which only
// the job staging the image knows.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "tests/test_utils/declared_topologies.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/simulation_discovery.hpp"
#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd-workload/chip/chip.hpp"
#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/cluster_descriptor.hpp"
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
using tt::umd::test_utils::DECLARED_BLACKHOLE_UNHARVESTED;
using tt::umd::test_utils::DECLARED_WORMHOLE_UNHARVESTED;
using tt::umd::test_utils::shipped_soc_descriptors;
using tt::umd::test_utils::ShippedSocDescriptorCase;
using tt::umd::test_utils::simulation_discovery_options;
using tt::umd::test_utils::SimulatorLayout;

// What a declared topology for one image looks like once discovered.
struct TTSimDeclaredTopologyCase {
    const char* yaml;
    std::unordered_map<ChipId, uint64_t> unique_ids;
    std::set<ChipId> remote_chips;
    // Host memory channels every MMIO chip gets when the caller leaves the count unset: the most chips one MMIO chip
    // serves, itself included.
    size_t host_mem_channels;
    // A one-chip descriptor of the other architecture, which the image cannot model.
    const char* other_arch_yaml;
    std::set<ChipId> mmio_chips;
    // The board id the image's firmware reports for every chip (specs/ttsim/multichip.md "Chip identity values").
    uint64_t board_id;
    // Tensix cores chip 0 has left after the image's harvesting (specs/ttsim/device-model.md "Harvesting").
    size_t chip0_tensix_cores;
};

namespace {

// Cluster descriptors declaring the topology each TTSim image compiles in: chips, MMIO chips, Ethernet links,
// harvesting and board ids as specs/ttsim/multichip.md and specs/ttsim/device-model.md give them. The
// chip_unique_ids are made up: discovery would read others off the device, so finding them in the result shows the
// descriptor came from the declaration.

constexpr const char* DECLARED_WORMHOLE_N150 = R"(arch:
  0: wormhole_b0
chips:
  0: [0, 0, 0, 0]
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 64}
boards:
  - - board_id: 1649267441665
    - board_type: n150
    - chips: [0]
)";

constexpr const char* DECLARED_WORMHOLE_N300 = R"(arch:
  0: wormhole_b0
  1: wormhole_b0
chips:
  0: [0, 0, 0, 0]
  1: [1, 0, 0, 0]
chip_unique_ids:
  0: 5550000
  1: 5550001
ethernet_connections:
  - [{chip: 0, chan: 8}, {chip: 1, chan: 0}]
  - [{chip: 0, chan: 9}, {chip: 1, chan: 1}]
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 65}
  1: {noc_translation: true, harvest_mask: 5}
boards:
  - - board_id: 72058994491072686
    - board_type: n300
    - chips: [0, 1]
)";

constexpr const char* DECLARED_BLACKHOLE_P150 = R"(arch:
  0: blackhole
chips: {}
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 192, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
boards:
  - - board_id: 4398046511105
    - board_type: p150
    - chips: [0]
)";

constexpr const char* DECLARED_BLACKHOLE_P300 = R"(arch:
  0: blackhole
  1: blackhole
chips: {}
chip_unique_ids:
  0: 5550000
  1: 5550001
ethernet_connections:
  - [{chip: 0, chan: 2}, {chip: 1, chan: 9}]
  - [{chip: 0, chan: 3}, {chip: 1, chan: 8}]
chips_with_mmio:
  - 0: 0
  - 1: 1
harvesting:
  0: {noc_translation: true, harvest_mask: 8320, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
  1: {noc_translation: true, harvest_mask: 272, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
boards:
  - - board_id: 4672924418049
    - board_type: p300
    - chips: [0, 1]
)";

// One chip whose Tensix columns are all declared live; its Ethernet channels 12 and 13 are harvested as on every BH
// image.
constexpr const char* DECLARED_BLACKHOLE_TENSIX_UNHARVESTED = R"(arch:
  0: blackhole
chips: {}
chip_unique_ids:
  0: 5550000
ethernet_connections: []
chips_with_mmio:
  - 0: 0
harvesting:
  0: {noc_translation: true, harvest_mask: 0, dram_harvesting_mask: 0, eth_harvesting_mask: 12288,
      pcie_harvesting_mask: 2}
boards:
  - - board_id: 4398046511105
    - board_type: p150
    - chips: [0]
)";

// Keyed by the image's architecture and chip count.
const std::map<std::pair<tt::ARCH, size_t>, TTSimDeclaredTopologyCase>& declared_topologies() {
    static const std::map<std::pair<tt::ARCH, size_t>, TTSimDeclaredTopologyCase> topologies = {
        {{tt::ARCH::WORMHOLE_B0, 1},
         {DECLARED_WORMHOLE_N150, {{0, 5550000}}, {}, 1, DECLARED_BLACKHOLE_P150, {0}, 1649267441665, 72}},
        {{tt::ARCH::WORMHOLE_B0, 2},
         {DECLARED_WORMHOLE_N300,
          {{0, 5550000}, {1, 5550001}},
          {1},
          2,
          DECLARED_BLACKHOLE_P150,
          {0},
          72058994491072686,
          64}},
        {{tt::ARCH::BLACKHOLE, 1},
         {DECLARED_BLACKHOLE_P150, {{0, 5550000}}, {}, 1, DECLARED_WORMHOLE_N150, {0}, 4398046511105, 120}},
        {{tt::ARCH::BLACKHOLE, 2},
         {DECLARED_BLACKHOLE_P300,
          {{0, 5550000}, {1, 5550001}},
          {},
          1,
          DECLARED_WORMHOLE_N150,
          {0, 1},
          4672924418049,
          120}},
    };
    return topologies;
}

ClusterOptions simulation_cluster_options(const std::filesystem::path& simulator_path) {
    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path;
    return options;
}

}  // namespace

// The TTSim image under test, its architecture and how many chips it models.
class TTSimImageTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr || !std::filesystem::is_regular_file(simulator_path)) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR does not name a TTSim library.";
        }
        image_ = simulator_path;
        arch_ = get_simulator_arch(image_);
        if (arch_ != tt::ARCH::WORMHOLE_B0 && arch_ != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "TTSim models neither ARC nor Ethernet for Quasar, so it has no topology to discover.";
        }
        const char* expected_chips = std::getenv("TT_UMD_SIM_EXPECTED_CHIPS");
        if (expected_chips == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIM_EXPECTED_CHIPS is not set, so the chips this image models are unknown.";
        }
        chips_ = static_cast<size_t>(std::stoul(expected_chips));
    }

    std::filesystem::path image_;
    tt::ARCH arch_ = tt::ARCH::Invalid;
    size_t chips_ = 0;
};

// A copy of the image under test with the topology of its kind declared beside it. The copy is a separate image to
// the loader, so it never shares state with the original another test brought up. The copy ships a SoC descriptor of
// its architecture, so its architecture is read from that file rather than by running the image after other
// simulators are already up in this process.
class TTSimDeclaredTopology : public TTSimImageTest {
protected:
    void SetUp() override {
        TTSimImageTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        const auto topology = declared_topologies().find({arch_, chips_});
        if (topology == declared_topologies().end()) {
            GTEST_SKIP() << "No topology to declare for a " << tt::arch_to_str(arch_) << " image of " << chips_
                         << " chip(s).";
        }
        declared_ = &topology->second;
    }

    // The copy with its architecture's SoC descriptor and yaml declared beside it.
    std::filesystem::path declare(const char* yaml) const { return declare(yaml, soc_descriptor_name()); }

    std::filesystem::path declare(const char* yaml, const std::string& soc_descriptor_name) const {
        layout_.add_cluster_descriptor(yaml);
        layout_.add_soc_descriptor(soc_descriptor_name);
        return layout_.add_ttsim_library(image_);
    }

    // The copy with its architecture's SoC descriptor but no declared topology.
    std::filesystem::path copy() const {
        layout_.add_soc_descriptor(soc_descriptor_name());
        return layout_.add_ttsim_library(image_);
    }

    std::string soc_descriptor_name() const {
        return arch_ == tt::ARCH::WORMHOLE_B0 ? "wormhole_b0_8x10.yaml" : "blackhole_140_arch.yaml";
    }

    SimulatorLayout layout_;
    const TTSimDeclaredTopologyCase* declared_ = nullptr;
};

TEST_F(TTSimDeclaredTopology, DiscoveryReturnsTheDeclaredDescriptor) {
    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(declared_->yaml)));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), declared_->unique_ids);
}

TEST_F(TTSimDeclaredTopology, DiscoveryOpensADeviceForEveryDeclaredChip) {
    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(declared_->yaml)));

    std::set<ChipId> opened;
    for (const auto& [chip, device] : devices) {
        ASSERT_NE(device, nullptr) << "chip " << chip;
        opened.insert(chip);
    }
    std::set<ChipId> declared;
    for (const auto& [chip, _] : declared_->unique_ids) {
        declared.insert(chip);
    }
    EXPECT_EQ(opened, declared);
}

// A chip the descriptor gives no MMIO endpoint is reached over Ethernet through its closest MMIO chip.
TEST_F(TTSimDeclaredTopology, DeclaredRemoteChipsAreRemoteDevices) {
    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(declared_->yaml)));

    for (const auto& [chip, device] : devices) {
        EXPECT_EQ(device->is_remote(), declared_->remote_chips.count(chip) != 0) << "chip " << chip;
    }
}

TEST_F(TTSimDeclaredTopology, DeclaredArchOtherThanTheImagesIsRefused) {
    const std::filesystem::path simulator = declare(declared_->other_arch_yaml);

    try {
        TopologyDiscovery::discover(simulation_discovery_options(simulator));
        FAIL() << "Discovery accepted a descriptor whose chip 0 the image cannot model.";
    } catch (const error::UmdException<error::RuntimeError>& e) {
        EXPECT_NE(std::string(e.what()).find("Cluster descriptor gives chip 0 architecture"), std::string::npos)
            << e.what();
    }
}

TEST_F(TTSimDeclaredTopology, DiscoveredDevicesAreInitialized) {
    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(declared_->yaml)));

    // The board id comes from the device's firmware, which an uninitialized device has not been asked.
    for (const auto& [chip, device] : devices) {
        EXPECT_EQ(device->get_board_id(), declared_->board_id) << "chip " << chip;
    }
}

TEST_F(TTSimDeclaredTopology, DiscoveryStampsTheCallersClusterId) {
    TopologyDiscoveryOptions options = simulation_discovery_options(declare(declared_->yaml));
    options.cluster_id = "ttsim-declared.cluster_1";

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(cluster_desc->get_cluster_id(), std::optional<std::string>("ttsim-declared.cluster_1"));
}

TEST_F(TTSimDeclaredTopology, CallersClusterIdReplacesTheDeclaredOne) {
    const std::string yaml_with_cluster_id = std::string("cluster_id: 'from-the-declaration'\n") + declared_->yaml;
    TopologyDiscoveryOptions options = simulation_discovery_options(declare(yaml_with_cluster_id.c_str()));
    options.cluster_id = "ttsim-declared.cluster_1";

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(cluster_desc->get_cluster_id(), std::optional<std::string>("ttsim-declared.cluster_1"));
}

TEST_F(TTSimDeclaredTopology, IllegalClusterIdIsRefused) {
    TopologyDiscoveryOptions options = simulation_discovery_options(declare(declared_->yaml));
    options.cluster_id = "not a cluster id!";

    EXPECT_ANY_THROW(TopologyDiscovery::discover(options));
}

TEST_F(TTSimDeclaredTopology, WithoutRemoteDiscoveryOnlyMmioChipsAreDiscovered) {
    TopologyDiscoveryOptions options = simulation_discovery_options(declare(declared_->yaml));
    options.discover_remote_devices = false;

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    const auto all_chips = cluster_desc->get_all_chips();
    EXPECT_EQ(std::set<ChipId>(all_chips.begin(), all_chips.end()), declared_->mmio_chips);
    std::set<ChipId> opened;
    for (const auto& [chip, _] : devices) {
        opened.insert(chip);
    }
    EXPECT_EQ(opened, declared_->mmio_chips);
}

TEST_F(TTSimDeclaredTopology, ClusterOpensTheDeclaredTopology) {
    Cluster cluster(simulation_cluster_options(declare(declared_->yaml)));

    EXPECT_EQ(cluster.get_cluster_description()->get_chip_unique_ids(), declared_->unique_ids);
}

TEST_F(TTSimDeclaredTopology, ClusterReadsBackWhatItWritesOnEveryDeclaredChip) {
    Cluster cluster(simulation_cluster_options(declare(declared_->yaml)));

    for (const auto& [chip, _] : declared_->unique_ids) {
        const CoreCoord core = cluster.get_soc_descriptor(chip).get_cores(CoreType::TENSIX).front();
        const uint32_t written = 0xC0DE0000 + chip;
        cluster.write_to_device(&written, sizeof(written), chip, core, 0x10000);
        uint32_t read = 0;
        cluster.read_from_device(&read, chip, core, 0x10000, sizeof(read));
        EXPECT_EQ(read, written) << "chip " << chip;
    }
}

TEST_F(TTSimDeclaredTopology, UnsetHostMemChannelsAreSizedFromTheDeclaredTopology) {
    Cluster cluster(simulation_cluster_options(declare(declared_->yaml)));

    for (const auto& [chip, _] : cluster.get_cluster_description()->get_chips_with_mmio()) {
        SysmemManager* sysmem_manager = cluster.get_chip(chip)->get_sysmem_manager();
        ASSERT_NE(sysmem_manager, nullptr) << "MMIO chip " << chip;
        EXPECT_EQ(sysmem_manager->get_num_host_mem_channels(), declared_->host_mem_channels) << "MMIO chip " << chip;
    }
}

// A caller's count is the count every MMIO chip gets, whatever the declared topology would size it to.
TEST_F(TTSimDeclaredTopology, ExplicitHostMemChannelsAreHonoured) {
    ClusterOptions options = simulation_cluster_options(declare(declared_->yaml));
    options.num_host_mem_ch_per_mmio_device = 3;
    Cluster cluster(options);

    for (const auto& [chip, _] : cluster.get_cluster_description()->get_chips_with_mmio()) {
        SysmemManager* sysmem_manager = cluster.get_chip(chip)->get_sysmem_manager();
        ASSERT_NE(sysmem_manager, nullptr) << "MMIO chip " << chip;
        EXPECT_EQ(sysmem_manager->get_num_host_mem_channels(), 3u) << "MMIO chip " << chip;
    }
}

TEST_F(TTSimDeclaredTopology, DiscoveryBuildsDevicesFromTheShippedSocDescriptor) {
    const ShippedSocDescriptorCase& shipped = shipped_soc_descriptors().at(arch_);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(
        simulation_discovery_options(declare(shipped.cluster_yaml, shipped.soc_descriptor_name)));

    EXPECT_EQ(devices.at(0)->get_soc_descriptor().get_cores(shipped.core_type).size(), shipped.cores);
}

TEST_F(TTSimDeclaredTopology, SocDescriptorOfAnotherArchIsRefused) {
    TopologyDiscoveryOptions options = simulation_discovery_options(declare(declared_->yaml));
    options.soc_descriptor_path = test_utils::get_soc_desc_path(
        arch_ == tt::ARCH::WORMHOLE_B0 ? "blackhole_140_arch.yaml" : "wormhole_b0_8x10.yaml");

    EXPECT_THROW(TopologyDiscovery::discover(options), error::UmdException<error::RuntimeError>);
}

// The declaration harvests no Tensix core, but the image harvests some on chip 0, and the device's SoC descriptor
// follows the image.
TEST_F(TTSimDeclaredTopology, SocDescriptorHarvestingComesFromTheDevice) {
    const char* unharvested =
        arch_ == tt::ARCH::WORMHOLE_B0 ? DECLARED_WORMHOLE_UNHARVESTED : DECLARED_BLACKHOLE_TENSIX_UNHARVESTED;

    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(unharvested)));

    EXPECT_EQ(devices.at(0)->get_soc_descriptor().get_cores(CoreType::TENSIX).size(), declared_->chip0_tensix_cores);
}

// Every Blackhole chip has Ethernet channels harvested, so a declaration harvesting none of the 14 channels its SoC
// descriptor gives is not one the architecture allows.
TEST_F(TTSimDeclaredTopology, DeclaredHarvestingTheArchDoesNotAllowIsRefused) {
    if (arch_ != tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Only Blackhole declares Ethernet harvesting.";
    }
    const std::filesystem::path simulator = declare(DECLARED_BLACKHOLE_UNHARVESTED);

    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(simulator)), error::UmdException<error::RuntimeError>);
}

// A caller-supplied descriptor (deprecated for SIMULATION) is still held to the image's architecture.
TEST_F(TTSimDeclaredTopology, ClusterRefusesACallerDescriptorOfAnotherArch) {
    const std::unique_ptr<ClusterDescriptor> other_arch =
        ClusterDescriptor::create_from_yaml_content(declared_->other_arch_yaml);
    ClusterOptions options = simulation_cluster_options(copy());
    options.cluster_descriptor = other_arch.get();

    EXPECT_THROW(Cluster cluster(options), error::UmdException<error::RuntimeError>);
}

// Selecting the last chip of a board brings in the rest of it: N150 and P150 are one chip, N300 and P300 two.
TEST_F(TTSimDeclaredTopology, TargetingOneChipDiscoversItsWholeBoard) {
    const ChipId last_chip = static_cast<ChipId>(chips_ - 1);

    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(declared_->yaml), {last_chip}));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), declared_->unique_ids);
    std::set<ChipId> declared;
    for (const auto& [chip, _] : declared_->unique_ids) {
        declared.insert(chip);
    }
    EXPECT_EQ(chips_of(devices), declared);
}

TEST_F(TTSimDeclaredTopology, UnknownTargetDeviceIsRefused) {
    const std::filesystem::path simulator = declare(declared_->yaml);

    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(simulator, {99})),
        error::UmdException<error::RuntimeError>);
}

// Declared chips on boards of their own, of which one is selected.
class TTSimDeclaredSingleChipBoards : public TTSimDeclaredTopology {
protected:
    // Every test selects its chips itself, so a TT_VISIBLE_DEVICES from the caller's environment is set aside for it.
    void SetUp() override {
        if (const char* visible_devices = std::getenv("TT_VISIBLE_DEVICES")) {
            saved_visible_devices_ = visible_devices;
        }
        unsetenv("TT_VISIBLE_DEVICES");
        TTSimDeclaredTopology::SetUp();
        if (IsSkipped()) {
            return;
        }
        if (arch_ != tt::ARCH::BLACKHOLE || chips_ != 2) {
            GTEST_SKIP() << "Only the bh_x2 image models two MMIO chips that can be declared as separate boards.";
        }
    }

    void TearDown() override {
        if (saved_visible_devices_.has_value()) {
            setenv("TT_VISIBLE_DEVICES", saved_visible_devices_->c_str(), 1);
        } else {
            unsetenv("TT_VISIBLE_DEVICES");
        }
    }

    std::optional<std::string> saved_visible_devices_;
};

TEST_F(TTSimDeclaredSingleChipBoards, TargetedChipIsRenumberedFromZero) {
    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(DECLARED_BLACKHOLE_TWO_P150_BOARDS), {1}));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550001}}));
    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

// bh_x2's firmware reports each chip's index as its ASIC location (specs/ttsim/multichip.md "Chip identity values"),
// so device 0 reporting 1 shows it drives the selected chip, not the image's chip 0.
TEST_F(TTSimDeclaredSingleChipBoards, TargetedChipsDeviceDrivesThatChip) {
    const auto [cluster_desc, devices] =
        TopologyDiscovery::discover(simulation_discovery_options(declare(DECLARED_BLACKHOLE_TWO_P150_BOARDS), {1}));

    EXPECT_EQ(devices.at(0)->get_asic_location(), 1);
}

TEST_F(TTSimDeclaredSingleChipBoards, VisibleDevicesSelectTheChipsWithoutTargets) {
    const std::filesystem::path simulator = declare(DECLARED_BLACKHOLE_TWO_P150_BOARDS);
    setenv("TT_VISIBLE_DEVICES", "1", 1);

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(simulator));

    EXPECT_EQ(cluster_desc->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550001}}));
    EXPECT_EQ(chips_of(devices), (std::set<ChipId>{0}));
}

// A Cluster hands its target devices to discovery, which renumbers the selected chip 0: chip 0 of the Cluster is the
// image's chip 1, which reports ASIC location 1 (specs/ttsim/multichip.md "Chip identity values").
TEST_F(TTSimDeclaredSingleChipBoards, ClusterTargetingChipOneDrivesThatChip) {
    ClusterOptions options = simulation_cluster_options(declare(DECLARED_BLACKHOLE_TWO_P150_BOARDS));
    options.target_devices = {1};
    Cluster cluster(options);

    EXPECT_EQ(cluster.get_target_device_ids(), (std::set<ChipId>{0}));
    EXPECT_EQ(
        cluster.get_cluster_description()->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550001}}));
    EXPECT_EQ(cluster.get_tt_device(0)->get_asic_location(), 1);
}

TEST_F(TTSimDeclaredSingleChipBoards, ClusterVisibleDevicesSelectTheChip) {
    const ClusterOptions options = simulation_cluster_options(declare(DECLARED_BLACKHOLE_TWO_P150_BOARDS));
    setenv("TT_VISIBLE_DEVICES", "1", 1);
    Cluster cluster(options);

    EXPECT_EQ(cluster.get_target_device_ids(), (std::set<ChipId>{0}));
    EXPECT_EQ(
        cluster.get_cluster_description()->get_chip_unique_ids(), (std::unordered_map<ChipId, uint64_t>{{0, 5550001}}));
    EXPECT_EQ(cluster.get_tt_device(0)->get_asic_location(), 1);
}

// The image under test as it ships, without a declared topology: discovery enumerates it and walks Ethernet.
class TTSimEnumeratedTopology : public TTSimImageTest {
protected:
    void SetUp() override {
        TTSimImageTest::SetUp();
        if (IsSkipped()) {
            return;
        }
        if (std::filesystem::exists(image_.parent_path() / "cluster_descriptor.yaml")) {
            GTEST_SKIP() << "A cluster_descriptor.yaml beside this image declares its topology.";
        }
    }
};

TEST_F(TTSimEnumeratedTopology, DiscoveryFindsEveryChipTheImageModels) {
    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(image_));

    EXPECT_EQ(cluster_desc->get_number_of_chips(), chips_);
    EXPECT_EQ(devices.size(), chips_);
}

// The wh_x2 and bh_x2 images are one two-chip board (N300, P300), so selecting chip 0 selects both chips.
TEST_F(TTSimEnumeratedTopology, TargetingChipZeroDiscoversItsWholeBoard) {
    if (chips_ > 2) {
        GTEST_SKIP() << "A galaxy image's boards are not one board per image.";
    }

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(simulation_discovery_options(image_, {0}));

    EXPECT_EQ(cluster_desc->get_number_of_chips(), chips_);
    EXPECT_EQ(devices.size(), chips_);
}

TEST_F(TTSimEnumeratedTopology, UnknownTargetDeviceIsRefused) {
    EXPECT_THROW(
        TopologyDiscovery::discover(simulation_discovery_options(image_, {99})),
        error::UmdException<error::RuntimeError>);
}

// TTSim runs in this process, so there is no simulator to leave for another process to launch.
TEST_F(TTSimEnumeratedTopology, TTSimIgnoresLaunchSimulator) {
    TopologyDiscoveryOptions options = simulation_discovery_options(image_);
    options.simulation->launch_simulator = false;

    const auto [cluster_desc, devices] = TopologyDiscovery::discover(options);

    EXPECT_EQ(devices.size(), chips_);
}
