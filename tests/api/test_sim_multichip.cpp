// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Tests for the simulation multichip core infrastructure:
// - SocDescriptor::is_core_of_type (moved from a local helper in tt_sim_tt_device.cpp)
// - TTSimCommunicator shared dlopen / select_chip_if_needed patterns
//
// The SocDescriptor tests run on any CI host (no hardware required).
// The communicator tests require TT_UMD_SIMULATOR and are skipped otherwise.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

#ifdef TT_UMD_BUILD_SIMULATION
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#endif

using namespace tt;
using namespace tt::umd;

// ---------------------------------------------------------------------------
// SocDescriptor::is_core_of_type
// ---------------------------------------------------------------------------

// Return a ChipInfo that satisfies per-arch harvesting constraints so that
// SocDescriptor construction does not throw.
//
// Blackhole: BlackholeCoordinateManager requires exactly 2 or NUM_ETH_CHANNELS
// ETH cores harvested on a full grid.  Use the minimal valid case (2 channels).
static ChipInfo make_valid_chip_info(ARCH arch) {
    ChipInfo info{};
    if (arch == ARCH::BLACKHOLE) {
        info.harvesting_masks.eth_harvesting_mask = 0x3;  // harvest channels 0 & 1
    }
    return info;
}

class IsCoreOfTypeTest : public ::testing::TestWithParam<ARCH> {};

TEST_P(IsCoreOfTypeTest, DramCoreIsIdentifiedCorrectly) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    // Every core returned by get_cores(DRAM, TRANSLATED) must satisfy is_core_of_type.
    auto dram_cores = soc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    ASSERT_FALSE(dram_cores.empty()) << "Expected at least one DRAM core for arch " << arch_to_str(arch);

    for (const auto& core : dram_cores) {
        tt_xy_pair xy{core.x, core.y};
        EXPECT_TRUE(soc.is_core_of_type(xy, CoreType::DRAM, CoordSystem::TRANSLATED))
            << "DRAM core (" << xy.x << ", " << xy.y << ") not recognized by is_core_of_type";
    }
}

TEST_P(IsCoreOfTypeTest, TensixCoreIsNotDram) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    auto tensix_cores = soc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
    if (tensix_cores.empty()) {
        GTEST_SKIP() << "No TENSIX cores for arch " << arch_to_str(arch);
    }

    for (const auto& core : tensix_cores) {
        tt_xy_pair xy{core.x, core.y};
        EXPECT_FALSE(soc.is_core_of_type(xy, CoreType::DRAM, CoordSystem::TRANSLATED))
            << "TENSIX core (" << xy.x << ", " << xy.y << ") wrongly identified as DRAM";
    }
}

TEST_P(IsCoreOfTypeTest, EthCoreIsIdentifiedCorrectly) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    auto eth_cores = soc.get_cores(CoreType::ETH, CoordSystem::TRANSLATED);
    if (eth_cores.empty()) {
        GTEST_SKIP() << "No ETH cores for arch " << arch_to_str(arch);
    }

    for (const auto& core : eth_cores) {
        tt_xy_pair xy{core.x, core.y};
        EXPECT_TRUE(soc.is_core_of_type(xy, CoreType::ETH, CoordSystem::TRANSLATED))
            << "ETH core (" << xy.x << ", " << xy.y << ") not recognized by is_core_of_type";
    }
}

TEST_P(IsCoreOfTypeTest, GarbageXYIsNotAnyKnownType) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path), make_valid_chip_info(arch));

    // Use an absurdly large coordinate that cannot belong to any real core.
    tt_xy_pair garbage{9999, 9999};
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::DRAM, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::TENSIX, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::ETH, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::ARC, CoordSystem::TRANSLATED));
    EXPECT_FALSE(soc.is_core_of_type(garbage, CoreType::PCIE, CoordSystem::TRANSLATED));
}

// QUASAR is included because is_core_of_type's DRAM-teleport path in
// tt_sim_tt_device.cpp special-cases it, and a quasar_simulation_1x1.yaml
// descriptor is available in the repo.
INSTANTIATE_TEST_SUITE_P(
    AllArchs,
    IsCoreOfTypeTest,
    ::testing::Values(ARCH::WORMHOLE_B0, ARCH::BLACKHOLE, ARCH::QUASAR),
    [](const ::testing::TestParamInfo<ARCH>& info) { return arch_to_str(info.param); });

// ---------------------------------------------------------------------------
// TTSimCommunicator: shared dlopen and chip selection
// These tests require a running simulator (TT_UMD_SIMULATOR env var).
// ---------------------------------------------------------------------------

#ifdef TT_UMD_BUILD_SIMULATION

class TTSimCommunicatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        simulator_path_ = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path_ == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not set. Skipping communicator tests.";
        }
    }

    const char* simulator_path_ = nullptr;
};

// Verify that TTSimTTDevice::create produces a non-null device.
TEST_F(TTSimCommunicatorTest, CreateSimDevice) {
    auto device = TTSimTTDevice::create(simulator_path_);
    ASSERT_NE(device, nullptr);
    // The device should have a valid soc descriptor.
    const auto& soc = device->get_soc_descriptor();
    EXPECT_NE(soc.arch, ARCH::Invalid);
}

// The endpoint count must come from the image itself: it is what tells UMD how many chips a
// multichip build models without a cluster descriptor to declare it.
TEST_F(TTSimCommunicatorTest, EnumerateMmioDeviceBdfs) {
    // Enumeration stands on its own: no device is constructed first, as on silicon.
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);

    // Every image exposes at least one endpoint, at bus 0 device 0. The count is a property of the
    // image, so it is reported rather than asserted.
    ASSERT_FALSE(bdfs.empty());
    EXPECT_EQ(bdfs.front(), 0u);
    EXPECT_LE(bdfs.size(), 32u);
    std::cout << "simulator exposes " << bdfs.size() << " host-visible PCI endpoint(s)" << std::endl;

    // Endpoints live on bus 0, device field in bits [7:3], function 0, ascending and unique.
    for (size_t i = 0; i < bdfs.size(); ++i) {
        EXPECT_EQ(bdfs[i] & 0xFF00u, 0u) << "endpoint " << i << " is not on bus 0";
        EXPECT_EQ(bdfs[i] & 0x7u, 0u) << "endpoint " << i << " is not function 0";
        if (i > 0) {
            EXPECT_GT(bdfs[i], bdfs[i - 1]) << "endpoints are not ascending";
        }
    }
}

// Verify that write_to_device after close_device() is a no-op (closed_ guard).
// Also verifies that a second close_device() and the destructor do not
// double-call pfn_libttsim_exit_ (shutdown() honours closed_).
TEST_F(TTSimCommunicatorTest, WriteAfterCloseIsNoOp) {
    auto device = TTSimTTDevice::create(simulator_path_);
    ASSERT_NE(device, nullptr);

    const auto& soc = device->get_soc_descriptor();
    auto dram_cores = soc.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        GTEST_SKIP() << "No DRAM cores; cannot run I/O test.";
    }

    tt_xy_pair target{dram_cores[0].x, dram_cores[0].y};

    // Write a sentinel before close.
    uint32_t sentinel = 0xCAFEBABE;
    device->write_to_device(&sentinel, target, 0, sizeof(sentinel));

    device->close_device();

    // write_to_device after close must not crash (closed_ guard makes it a no-op).
    uint32_t after_close = 0xDEADBEEF;
    EXPECT_NO_THROW(device->write_to_device(&after_close, target, 0, sizeof(after_close)));

    // A second close_device() must not crash either (shutdown() checks closed_).
    EXPECT_NO_THROW(device->close_device());
    // Destructor also calls shutdown() — again honoured by the closed_ guard.
}

// Two TTSimTTDevice instances with distinct chip_ids writing and reading back
// independently. Exercises select_chip_if_needed for each I/O call.
TEST_F(TTSimCommunicatorTest, TwoDevicesIndependentIO) {
    // Enumerate before bringing anything up: the simulator aborts the process rather than throwing
    // when asked to start twice, so a single-endpoint image has to be detected up front.
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);
    if (bdfs.size() < 2) {
        GTEST_SKIP() << "Simulator image exposes " << bdfs.size()
                     << " endpoint(s); multi-device I/O needs at least two.";
    }
    const size_t num_chips = bdfs.size();
    const auto endpoint_count = static_cast<uint32_t>(bdfs.size());

    // The endpoint count selects shared-BDF addressing: the two devices share one image.
    auto dev_0 = TTSimTTDevice::create_for_chip(
        simulator_path_,
        static_cast<ChipId>(0),
        /*num_host_mem_channels=*/0,
        /*copy_sim_binary=*/false,
        num_chips,
        endpoint_count);
    ASSERT_NE(dev_0, nullptr);
    auto dev_1 = TTSimTTDevice::create_for_chip(
        simulator_path_,
        static_cast<ChipId>(1),
        /*num_host_mem_channels=*/0,
        /*copy_sim_binary=*/false,
        num_chips,
        endpoint_count);
    ASSERT_NE(dev_1, nullptr);

    const auto& soc_0 = dev_0->get_soc_descriptor();
    auto dram_cores = soc_0.get_cores(CoreType::DRAM, CoordSystem::TRANSLATED);
    if (dram_cores.empty()) {
        GTEST_SKIP() << "No DRAM cores; cannot run I/O test.";
    }

    tt_xy_pair target{dram_cores[0].x, dram_cores[0].y};

    // Write a pattern to device 0.
    uint32_t pattern_0 = 0xDEAD0000;
    dev_0->write_to_device(&pattern_0, target, 0, sizeof(pattern_0));

    // Write a different pattern to device 1 at the same address.
    uint32_t pattern_1 = 0xBEEF0001;
    dev_1->write_to_device(&pattern_1, target, 0, sizeof(pattern_1));

    // Read back from device 0 — should still see pattern_0.
    uint32_t readback_0 = 0;
    dev_0->read_from_device(&readback_0, target, 0, sizeof(readback_0));
    EXPECT_EQ(readback_0, pattern_0);

    // Read back from device 1 — should see pattern_1.
    uint32_t readback_1 = 0;
    dev_1->read_from_device(&readback_1, target, 0, sizeof(readback_1));
    EXPECT_EQ(readback_1, pattern_1);

    dev_0->close_device();
    dev_1->close_device();
}

// ---------------------------------------------------------------------------
// Topology discovery against a simulator image
// ---------------------------------------------------------------------------

// A simulator with no cluster_descriptor.yaml beside it has its topology discovered rather than
// declared, so what the descriptor says is a property of the image. These are the invariants.
class TTSimDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* simulator_path = std::getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR is not set. Skipping discovery tests.";
        }
        simulator_path_ = simulator_path;

        if (std::filesystem::exists(SimulationChip::get_cluster_descriptor_path_from_simulator_path(simulator_path_))) {
            GTEST_SKIP() << "A cluster_descriptor.yaml sits beside this simulator, so its topology is declared "
                            "rather than discovered.";
        }

        arch_ = SocDescriptor::get_arch_from_soc_descriptor_path(
            SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_path_));
        if (arch_ == ARCH::QUASAR) {
            GTEST_SKIP() << "TTSim models neither ARC nor Ethernet for Quasar, so there is no firmware to discover "
                            "a topology from.";
        }
    }

    std::string simulator_path_;
    ARCH arch_ = ARCH::Invalid;
};

TEST_F(TTSimDiscoveryTest, ChipCountMatchesEnumeratedEndpoints) {
    const std::vector<uint32_t> bdfs = TTSimCommunicator::enumerate_mmio_device_bdfs(simulator_path_);
    ASSERT_FALSE(bdfs.empty());

    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path_;
    // Left empty so every discovered chip is visible; the shared helper constrains to chip 0.
    options.num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster(options);

    ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    ASSERT_NE(cluster_desc, nullptr);

    // One chip per host-visible endpoint, all of them MMIO: every chip a TTSim image exposes to the
    // host is reachable over its own BDF, so discovery finding a different number means it either
    // missed one or invented one.
    EXPECT_EQ(cluster_desc->get_number_of_chips(), bdfs.size());
    EXPECT_EQ(cluster_desc->get_chips_with_mmio().size(), bdfs.size());
    for (const ChipId chip : cluster_desc->get_all_chips()) {
        EXPECT_TRUE(cluster_desc->is_chip_mmio_capable(chip)) << "chip " << chip << " is not MMIO capable";
    }
}

TEST_F(TTSimDiscoveryTest, HarvestingComesFromTheDevice) {
    if (arch_ != ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Harvesting expectations below are Blackhole's.";
    }

    ClusterOptions options;
    options.chip_type = ChipType::SIMULATION;
    options.simulator_directory = simulator_path_;
    options.num_host_mem_ch_per_mmio_device = 1;
    Cluster cluster(options);

    ClusterDescriptor* cluster_desc = cluster.get_cluster_description();
    ASSERT_NE(cluster_desc, nullptr);
    ASSERT_FALSE(cluster_desc->get_all_chips().empty());

    for (const ChipId chip : cluster_desc->get_all_chips()) {
        // TTSim models ETH tiles 12 and 13 as harvested, so its ENABLED_ETH telemetry reads 0x0FFF
        // and the mask UMD derives from it is 0x3000. Asserting the derived value is what keeps
        // discovery honest about reading harvesting from the device instead of assuming it.
        EXPECT_EQ(cluster_desc->get_harvesting_masks(chip).eth_harvesting_mask, 0x3000u)
            << "chip " << chip << " ETH harvesting did not come from telemetry";

        // Whatever the image harvests, the descriptor and the SoC descriptor built from it have to
        // tell the same story about how many Tensix columns survived.
        const SocDescriptor& soc_desc = cluster.get_soc_descriptor(chip);
        EXPECT_FALSE(soc_desc.get_cores(CoreType::TENSIX).empty()) << "chip " << chip << " has no Tensix cores";
    }
}

#endif  // TT_UMD_BUILD_SIMULATION
