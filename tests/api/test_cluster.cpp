// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "test_utils/setup_risc_cores.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "utils.hpp"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy.

// This test should be one line only.
TEST(ApiClusterTest, OpenAllSiliconChips) { std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(); }

TEST(TestCluster, PrintAllSiliconChipsAllCores) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    for (ChipId chip : umd_cluster->get_target_device_ids()) {
        std::cout << "Chip " << chip << std::endl;

        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip);

        const std::vector<CoreCoord>& tensix_cores = soc_desc.get_cores(CoreType::TENSIX);
        for (const CoreCoord& core : tensix_cores) {
            std::cout << "Tensix core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_tensix_cores = soc_desc.get_harvested_cores(CoreType::TENSIX);
        for (const CoreCoord& core : harvested_tensix_cores) {
            std::cout << "Harvested Tensix core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& dram_cores = soc_desc.get_cores(CoreType::DRAM);
        for (const CoreCoord& core : dram_cores) {
            std::cout << "DRAM core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_dram_cores = soc_desc.get_harvested_cores(CoreType::DRAM);
        for (const CoreCoord& core : harvested_dram_cores) {
            std::cout << "Harvested DRAM core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& eth_cores = soc_desc.get_cores(CoreType::ETH);
        for (const CoreCoord& core : eth_cores) {
            std::cout << "ETH core " << core.str() << std::endl;
        }

        const std::vector<CoreCoord>& harvested_eth_cores = soc_desc.get_harvested_cores(CoreType::ETH);
        for (const CoreCoord& core : harvested_eth_cores) {
            std::cout << "Harvested ETH core " << core.str() << std::endl;
        }
    }
}

TEST(TestCluster, TestClusterAICLKControl) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    auto get_expected_clock_val = [&cluster](ChipId chip_id, bool busy) {
        tt::ARCH arch = cluster->get_cluster_description()->get_arch(chip_id);
        if (arch == tt::ARCH::WORMHOLE_B0) {
            return busy ? wormhole::AICLK_BUSY_VAL : wormhole::AICLK_IDLE_VAL;
        } else if (arch == tt::ARCH::BLACKHOLE) {
            return busy ? blackhole::AICLK_BUSY_VAL : blackhole::AICLK_IDLE_VAL;
        }
        return 0u;
    };

    cluster->set_power_state(DevicePowerState::BUSY);

    auto clocks_busy = cluster->get_clocks();
    for (auto& clock : clocks_busy) {
        // TODO #781: Figure out a proper mechanism to detect the right value. For now just check that Busy value is
        // larger than Idle value.
        EXPECT_GT(clock.second, get_expected_clock_val(clock.first, false));
    }

    cluster->set_power_state(DevicePowerState::LONG_IDLE);

    auto clocks_idle = cluster->get_clocks();
    for (auto& clock : clocks_idle) {
        EXPECT_EQ(clock.second, get_expected_clock_val(clock.first, false));
    }
}

TEST(TestCluster, GetEthernetFirmware) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    // BoardType P100 doesn't have eth cores.
    std::optional<SemVer> eth_version;
    EXPECT_NO_THROW(eth_version = cluster->get_ethernet_firmware_version());
    if (cluster->get_cluster_description()->get_board_type(0) == BoardType::P100) {
        EXPECT_FALSE(eth_version.has_value());
    } else {
        EXPECT_TRUE(eth_version.has_value());
    }
}
