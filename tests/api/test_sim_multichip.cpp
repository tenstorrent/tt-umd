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
#include <memory>
#include <string>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"

#ifdef TT_UMD_BUILD_SIMULATION
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
    // Use the chip_id overload so the two devices target different simulated chips.
    auto dev_0 = TTSimTTDevice::create_for_chip(simulator_path_, /* chip_id= */ static_cast<ChipId>(0));
    ASSERT_NE(dev_0, nullptr);

    // If the simulator binary is single-chip, we cannot open a second device.
    std::unique_ptr<TTSimTTDevice> dev_1;
    try {
        dev_1 = TTSimTTDevice::create_for_chip(simulator_path_, /* chip_id= */ static_cast<ChipId>(1));
    } catch (const std::exception&) {
        GTEST_SKIP() << "Simulator does not support multiple devices; skipping multi-device I/O test.";
    }
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

#endif  // TT_UMD_BUILD_SIMULATION
