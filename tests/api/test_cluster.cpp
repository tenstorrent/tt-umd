// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/topology/topology_discovery_options.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/semver.hpp"

using namespace tt;
using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy.

// This test should be one line only.
TEST(ApiClusterTest, OpenAllSiliconChips) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();
}

TEST(TestCluster, PrintAllSiliconChipsAllCores) {
    std::unique_ptr<Cluster> umd_cluster = test_utils::make_default_test_cluster();

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
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    auto get_expected_clock_val = [&cluster](ChipId chip_id, bool busy) {
        tt::ARCH arch = cluster->get_cluster_description()->get_arch(chip_id);
        if (arch == tt::ARCH::WORMHOLE_B0) {
            return busy ? wormhole::AICLK_BUSY_VAL : wormhole::AICLK_IDLE_VAL;
        } else if (arch == tt::ARCH::BLACKHOLE) {
            return busy ? blackhole::AICLK_BUSY_VAL : blackhole::AICLK_IDLE_VAL;
        }
        return 0u;
    };

    cluster->set_clock_state(DevicePowerState::BUSY);

    auto clocks_busy = cluster->get_clocks();
    for (auto& clock : clocks_busy) {
        // TODO #781: Figure out a proper mechanism to detect the right value. For now just check that Busy value is
        // larger than Idle value.
        EXPECT_GT(clock.second, get_expected_clock_val(clock.first, false));
    }

    cluster->set_clock_state(DevicePowerState::LONG_IDLE);

    auto clocks_idle = cluster->get_clocks();
    for (auto& clock : clocks_idle) {
        EXPECT_EQ(clock.second, get_expected_clock_val(clock.first, false));
    }
}

TEST(TestCluster, RefreshClusterDescriptionDoesNotThrow) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    EXPECT_NO_THROW(cluster->refresh_cluster_description());
}

TEST(TestCluster, GetEthernetFirmware) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    // BoardType P100 doesn't have eth cores.
    std::optional<SemVer> eth_version;
    EXPECT_NO_THROW(eth_version = cluster->get_ethernet_firmware_version());
    if (cluster->get_cluster_description()->get_board_type(0) == BoardType::P100) {
        EXPECT_FALSE(eth_version.has_value());
    } else {
        EXPECT_TRUE(eth_version.has_value());
    }
}

TEST(TestCluster, TestDifferentPowerModes) {
    {
        if (PCIDevice::get_pcie_arch() != tt::ARCH::BLACKHOLE) {
            GTEST_SKIP() << "Different power modes is supported only for Blackhole.";
        }
    }

    {
        TopologyDiscoveryOptions default_options;
        auto [desc_default, devices_default] = TopologyDiscovery::discover(default_options);
        for (auto& [chip_id, tt_device] : devices_default) {
            ArcTelemetryReader* telemetry_reader = tt_device->get_arc_telemetry_reader();
            uint32_t power = telemetry_reader->read_entry(TelemetryTag::INPUT_POWER);
            std::cout << "Default mode - Chip " << chip_id << " power: " << power << std::endl;
        }
    }

    {
        TopologyDiscoveryOptions power_options;
        power_options.low_power = true;
        auto [desc_low_power, devices_low_power] = TopologyDiscovery::discover(power_options);
        for (auto& [chip_id, tt_device] : devices_low_power) {
            ArcTelemetryReader* telemetry_reader = tt_device->get_arc_telemetry_reader();
            uint32_t power = telemetry_reader->read_entry(TelemetryTag::INPUT_POWER);
            std::cout << "Low power mode - Chip " << chip_id << " power: " << power << std::endl;
        }
    }

    {
        std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
        for (ChipId chip_id : cluster->get_target_device_ids()) {
            TTDevice* tt_device = cluster->get_tt_device(chip_id);
            ArcTelemetryReader* telemetry_reader = tt_device->get_arc_telemetry_reader();
            uint32_t power = telemetry_reader->read_entry(TelemetryTag::INPUT_POWER);
            std::cout << "Cluster mode - Chip " << chip_id << " power: " << power << std::endl;
        }
    }
}
