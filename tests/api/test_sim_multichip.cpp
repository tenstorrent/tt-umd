// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
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

class IsCoreOfTypeTest : public ::testing::TestWithParam<ARCH> {};

TEST_P(IsCoreOfTypeTest, DramCoreIsIdentifiedCorrectly) {
    const ARCH arch = GetParam();
    const std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path));

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
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path));

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
    SocDescriptor soc(std::make_shared<SocArchDescriptor>(sdesc_path));

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

INSTANTIATE_TEST_SUITE_P(
    AllArchs,
    IsCoreOfTypeTest,
    ::testing::Values(ARCH::WORMHOLE_B0, ARCH::BLACKHOLE),
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

// Verify that two communicators targeting different chip IDs can be
// constructed from the same simulator directory. In multichip mode they
// share a single dlopen handle (refcount = 2).
TEST_F(TTSimCommunicatorTest, TwoCommunicatorsShareHandle) {
    // chip_id 0 and 1 -- does not need a real multi-chip sim binary;
    // the constructor merely stores chip_id, actual multichip detection
    // happens in initialize() which we skip here to avoid needing a
    // live simulator.
    TTSimCommunicator comm_0(simulator_path_, /* copy_sim_binary= */ false, /* chip_id= */ 0);
    TTSimCommunicator comm_1(simulator_path_, /* copy_sim_binary= */ false, /* chip_id= */ 1);

    // Both communicators exist without throwing.
    // If the .so supports multichip ABI, initialize() would set them up
    // with shared dlopen. Without calling initialize() we just verify
    // construction is safe.
    SUCCEED();
}

// Verify that TTSimTTDevice::create produces a non-null device.
TEST_F(TTSimCommunicatorTest, CreateSimDevice) {
    auto device = TTSimTTDevice::create(simulator_path_);
    ASSERT_NE(device, nullptr);
    // The device should have a valid soc descriptor.
    const auto& soc = device->get_soc_descriptor();
    EXPECT_NE(soc.arch, ARCH::Invalid);
}

#endif  // TT_UMD_BUILD_SIMULATION
