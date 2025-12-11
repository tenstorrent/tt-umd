// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>  // For access()

#include <algorithm>
#include <cstdint>
#include <cstdlib>  // for std::getenv
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "test_utils/assembly_programs_for_tests.hpp"
#include "test_utils/setup_risc_cores.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/chip/mock_chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/warm_reset.hpp"
#include "utils.hpp"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy.

constexpr std::uint32_t L1_BARRIER_BASE = 12;
constexpr std::uint32_t ETH_BARRIER_BASE = 256 * 1024 - 32;
constexpr std::uint32_t DRAM_BARRIER_BASE = 0;

std::vector<ClusterOptions> get_cluster_options_for_param_test() {
    constexpr const char* TT_UMD_SIMULATOR_ENV = "TT_UMD_SIMULATOR";
    std::vector<ClusterOptions> options;
    options.push_back(ClusterOptions{.chip_type = ChipType::SILICON});
    if (std::getenv(TT_UMD_SIMULATOR_ENV)) {
        options.push_back(ClusterOptions{
            .chip_type = ChipType::SIMULATION,
            .target_devices = {0},
            .simulator_directory = std::filesystem::path(std::getenv(TT_UMD_SIMULATOR_ENV))});
    }
    return options;
}

// Small helper function to check if the ipmitool is ready.
bool is_ipmitool_ready() {
    if (system("which ipmitool > /dev/null 2>&1") != 0) {
        std::cout << "ipmitool executable not found." << std::endl;
        return false;
    }

    if ((access("/dev/ipmi0", F_OK) != 0) && (access("/dev/ipmi/0", F_OK) != 0) &&
        (access("/dev/ipmidev/0", F_OK) != 0)) {
        std::cout << "IPMI device file not found (/dev/ipmi0, /dev/ipmi/0, or /dev/ipmidev/0)." << std::endl;
        return false;
    }

    return true;
}

// This test should be one line only.
TEST(ApiClusterTest, OpenAllSiliconChips) { std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(); }

TEST(ApiClusterTest, OpenChipsByPciId) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    // T3K and 4U have 4 PCIE visible devices each. After 4 devices, the next number is 32
    // on 6U galaxies. Making 2^32 combinations might take too long, so we limit the number of devices to 4.
    // TODO: test all combinations on 6U and remove this check if possible.
    if (pci_device_ids.size() > 4) {
        GTEST_SKIP() << "Skipping test because there are more than 4 PCI devices. "
                        "This test is intended to be run on all systems apart from 6U.";
    }

    int total_combinations = 1 << pci_device_ids.size();

    for (uint32_t combination = 0; combination < total_combinations; combination++) {
        std::unordered_set<int> target_pci_device_ids;
        for (int i = 0; i < pci_device_ids.size(); i++) {
            if (combination & (1 << i)) {
                target_pci_device_ids.insert(pci_device_ids[i]);
            }
        }

        std::cout << "Creating Cluster with target PCI device IDs: ";
        for (const auto& id : target_pci_device_ids) {
            std::cout << id << " ";
        }
        std::cout << std::endl;

        std::string value = test_utils::convert_to_comma_separated_string(target_pci_device_ids);

        if (setenv(utils::TT_VISIBLE_DEVICES_ENV.data(), value.c_str(), 1) != 0) {
            ASSERT_TRUE(false) << "Failed to unset environment variable.";
        }

        // Make sure that Cluster construction is without exceptions.
        // TODO: add cluster descriptors for expected topologies, compare cluster desc against expected desc.
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

        if (!target_pci_device_ids.empty()) {
            // If target_pci_device_ids is empty, then full cluster will be created, so skip the check.
            // Check that the cluster has the expected number of chips.
            auto actual_pci_device_ids = cluster->get_target_mmio_device_ids();
            EXPECT_EQ(actual_pci_device_ids.size(), target_pci_device_ids.size());
            // Always expect logical id 0 to exist, that's the way filtering by pci ids work.
            EXPECT_TRUE(actual_pci_device_ids.find(0) != actual_pci_device_ids.end());
        }

        if (unsetenv(utils::TT_VISIBLE_DEVICES_ENV.data()) != 0) {
            ASSERT_TRUE(false) << "Failed to unset environment variable.";
        }
    }
}

TEST(ApiClusterTest, OpenClusterByLogicalID) {
    // First, pregenerate a cluster descriptor and save it to a file.
    // This will run topology discovery and touch all the devices.
    std::filesystem::path cluster_path = Cluster::create_cluster_descriptor()->serialize_to_file();

    // Now, the user can create the cluster descriptor without touching the devices.
    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(cluster_path);
    // You can test the cluster descriptor here to see if the topology matched the one you'd expect.
    // For example, you can check if the number of chips is correct, or number of pci devices, or nature of eth
    // connections.
    std::unordered_set<ChipId> all_chips = cluster_desc->get_all_chips();
    std::unordered_map<ChipId, ChipId> chips_with_pcie = cluster_desc->get_chips_with_mmio();
    auto eth_connections = cluster_desc->get_ethernet_connections();

    if (all_chips.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    // Now we can choose which chips to open. This can be hardcoded if you already have expected topology.
    // The first cluster will open the first chip only, and the second cluster will open the rest of them.
    ChipId first_chip_only = chips_with_pcie.begin()->first;
    std::unique_ptr<Cluster> umd_cluster1 = std::make_unique<Cluster>(ClusterOptions{
        .target_devices = {first_chip_only},
        .cluster_descriptor = cluster_desc.get(),
    });

    auto chips1 = umd_cluster1->get_target_device_ids();
    EXPECT_EQ(chips1.size(), 1);
    EXPECT_EQ(*chips1.begin(), first_chip_only);

    std::unordered_set<ChipId> other_chips;
    for (auto chip : all_chips) {
        // Skip the first chip, but also skip all remote chips so that we don't accidentally hit the one tied to the
        // first local chip.
        if (chip != first_chip_only && cluster_desc->is_chip_mmio_capable(chip)) {
            other_chips.insert(chip);
        }
    }
    // Continue the test only if there there is more than one card in the system.
    if (!other_chips.empty()) {
        std::unique_ptr<Cluster> umd_cluster2 = std::make_unique<Cluster>(ClusterOptions{
            .target_devices = other_chips,
            .cluster_descriptor = cluster_desc.get(),
        });

        // Cluster 2 should have the rest of the chips and not contain the first chip.
        auto chips2 = umd_cluster2->get_target_device_ids();
        EXPECT_EQ(chips2.size(), chips_with_pcie.size() - 1);
        EXPECT_TRUE(chips2.find(first_chip_only) == chips2.end());
    }
}

TEST(ApiClusterTest, DifferentConstructors) {
    std::unique_ptr<Cluster> umd_cluster;

    // 1. Simplest constructor. Creates Cluster with all the chips available.
    umd_cluster = std::make_unique<Cluster>();
    bool chips_available = !umd_cluster->get_target_device_ids().empty();
    umd_cluster = nullptr;

    if (chips_available) {
        // 2. Constructor which allows choosing a subset of Chips to open.
        umd_cluster = std::make_unique<Cluster>(ClusterOptions{
            .target_devices = {0},
        });
        EXPECT_EQ(umd_cluster->get_target_device_ids().size(), 1);
        umd_cluster = nullptr;

        // 3. Constructor taking a custom soc descriptor in addition.
        tt::ARCH device_arch = Cluster::create_cluster_descriptor()->get_arch(0);
        // You can add a custom soc descriptor here.
        std::string sdesc_path = SocDescriptor::get_soc_descriptor_path(device_arch);
        umd_cluster = std::make_unique<Cluster>(ClusterOptions{
            .sdesc_path = sdesc_path,
        });
        umd_cluster = nullptr;
    }

    // 4. Constructor taking cluster descriptor based on which to create cluster.
    // This could be cluster descriptor cached from previous runtime, or with some custom modifications.
    // You can just create a cluster descriptor and serialize it to file, or fetch a cluster descriptor from already
    // created Cluster class.
    std::filesystem::path cluster_path1 = Cluster::create_cluster_descriptor()->serialize_to_file();
    umd_cluster = std::make_unique<Cluster>();
    std::filesystem::path cluster_path2 = umd_cluster->get_cluster_description()->serialize_to_file();
    umd_cluster = nullptr;

    std::unique_ptr<ClusterDescriptor> cluster_desc = ClusterDescriptor::create_from_yaml(cluster_path1);
    umd_cluster = std::make_unique<Cluster>(ClusterOptions{
        .cluster_descriptor = cluster_desc.get(),
    });
    umd_cluster = nullptr;

    // 5. Create mock chips is set to true in order to create mock chips for the devices in the cluster descriptor.
    umd_cluster = std::make_unique<Cluster>(ClusterOptions{
        .chip_type = ChipType::MOCK,
        .target_devices = {0},
    });
    umd_cluster = nullptr;
}

TEST(ApiClusterTest, SimpleIOAllSiliconChips) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    const ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size);

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, RemoteFlush) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    const ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_remote_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        if (!cluster_desc->is_chip_remote(chip_id)) {
            std::cout << "Chip " << chip_id << " skipped because it is not a remote chip." << std::endl;
            continue;
        }

        if (soc_desc.arch != tt::ARCH::WORMHOLE_B0) {
            std::cout << "Skipping remote chip " << chip_id << " because it is not a wormhole_b0 chip." << std::endl;
            continue;
        }

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;
        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        std::cout << "Waiting for remote chip flush " << chip_id << std::endl;
        umd_cluster->wait_for_non_mmio_flush(chip_id);

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;
        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size);

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, SimpleIOSpecificSiliconChips) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>(ClusterOptions{
        .target_devices = {0},
    });

    const ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size);

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ClusterAPI, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs
    // to be reconfigured for each transaction

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    DeviceParams default_params;
    cluster->start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = zeros;

    static const uint32_t num_loops = 100;

    for (const ChipId chip : cluster->get_target_device_ids()) {
        // Just make sure to skip L1_BARRIER_BASE.
        std::uint32_t address = 0x100;
        // Write to each core a 100 times at different statically mapped addresses.
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip);
        std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);
        for (int loop = 0; loop < num_loops; loop++) {
            for (auto& core : tensix_cores) {
                cluster->write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip, core, address);

                // Barrier to ensure that all writes over ethernet were commited.
                cluster->wait_for_non_mmio_flush();
                cluster->read_from_device(readback_vec.data(), chip, core, address, 40);

                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.x << "-" << core.y << "does not match what was written";

                cluster->wait_for_non_mmio_flush();

                cluster->write_to_device(zeros.data(), zeros.size() * sizeof(std::uint32_t), chip, core, address);

                cluster->wait_for_non_mmio_flush();

                readback_vec = zeros;
            }
            address += 0x20;  // Increment by uint32_t size for each write
        }
    }
    cluster->close_device();
}

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

// It is expected that logical ETH channel numbers are in the range [0, num_channels) for each
// chip. This is needed because of eth id readouts for Blackhole that don't take harvesting
// into acount. This test verifies that both for Wormhole and Blackhole.
TEST(TestCluster, TestClusterLogicalETHChannelsConnectivity) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    ClusterDescriptor* cluster_desc = cluster->get_cluster_description();

    for (auto [chip, connections] : cluster_desc->get_ethernet_connections()) {
        const uint32_t num_channels_local_chip = cluster->get_soc_descriptor(chip).get_cores(CoreType::ETH).size();
        for (auto [channel, remote_chip_and_channel] : connections) {
            auto [remote_chip, remote_channel] = remote_chip_and_channel;

            const uint32_t num_channels_remote_chip =
                cluster->get_soc_descriptor(remote_chip).get_cores(CoreType::ETH).size();

            EXPECT_TRUE(channel < num_channels_local_chip);
            EXPECT_TRUE(remote_channel < num_channels_remote_chip);
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

TEST(TestCluster, DISABLED_WarmResetScratch) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    uint32_t write_test_data = 0xDEADBEEF;

    auto chip_id = *cluster->get_target_device_ids().begin();
    auto tt_device = cluster->get_chip(chip_id)->get_tt_device();

    tt_device->bar_write32(
        tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
            tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset(),
        write_test_data);

    WarmReset::warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();
    chip_id = *cluster->get_target_device_ids().begin();
    tt_device = cluster->get_chip(chip_id)->get_tt_device();

    auto read_test_data = tt_device->bar_read32(
        tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
        tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset());

    EXPECT_NE(write_test_data, read_test_data);
}

TEST(TestCluster, GalaxyWarmResetScratch) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    static constexpr uint32_t DEFAULT_VALUE_IN_SCRATCH_REGISTER = 0;

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (!is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Only galaxy test configuration.";
    }

    auto arch = cluster->get_cluster_description()->get_arch();
    if (arch != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "Only test Wormhole architecture for Galaxy UBB reset.";
    }

    if (!is_ipmitool_ready()) {
        GTEST_SKIP() << "Only test warm reset on systems that have the ipmi tool.";
    }

    static constexpr uint32_t write_test_data = 0xDEADBEEF;

    for (auto& chip_id : cluster->get_target_mmio_device_ids()) {
        auto tt_device = cluster->get_chip(chip_id)->get_tt_device();
        tt_device->bar_write32(
            tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
                tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset(),
            write_test_data);
    }

    WarmReset::ubb_warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();

    for (auto& chip_id : cluster->get_target_mmio_device_ids()) {
        auto tt_device = cluster->get_chip(chip_id)->get_tt_device();

        auto read_test_data = tt_device->bar_read32(
            tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
            tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset());

        EXPECT_NE(write_test_data, read_test_data);
        EXPECT_EQ(DEFAULT_VALUE_IN_SCRATCH_REGISTER, read_test_data);
    }
}

TEST(TestCluster, WarmReset) {
    if constexpr (is_arm_platform()) {
        GTEST_SKIP() << "Warm reset is disabled on ARM64 due to instability.";
    }
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    auto arch = cluster->get_tt_device(0)->get_arch();
    if (arch == tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP()
            << "This test intentionally hangs the NOC. On Wormhole, this can cause a severe failure where even a warm "
               "reset does not recover the device, requiring a watchdog-triggered reset for recovery.";
    }

    std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> zero_data(data.size(), 0);
    std::vector<uint8_t> readback_data(data.size(), 0);

    // send data to core 15, 15 which will hang the NOC
    auto hanged_chip_id = *cluster->get_target_device_ids().begin();
    auto hanged_tt_device = cluster->get_chip(hanged_chip_id)->get_tt_device();
    hanged_tt_device->write_to_device(data.data(), {15, 15}, 0, data.size());

    // TODO: Remove this check when it is figured out why there is no hang detected on Blackhole.
    if (arch == tt::ARCH::WORMHOLE_B0) {
        EXPECT_THROW(hanged_tt_device->detect_hang_read(), std::runtime_error);
    }

    WarmReset::warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();

    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after reset.";

    // TODO: Comment this out after finding out how to detect hang reads on
    // EXPECT_NO_THROW(cluster->get_chip(0)->get_tt_device()->detect_hang_read());.

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);

            RiscType select_all_tensix_riscv_cores{RiscType::ALL_TENSIX};

            // Set all riscs to reset state.
            cluster->assert_risc_reset(chip_id, tensix_core, select_all_tensix_riscv_cores);

            cluster->l1_membar(chip_id, {tensix_core});

            // Zero out first 8 bytes on L1.
            cluster->write_to_device(zero_data.data(), zero_data.size(), chip_id, tensix_core, 0);

            cluster->write_to_device(data.data(), data.size(), chip_id, tensix_core, 0);

            cluster->read_from_device(readback_data.data(), chip_id, tensix_core, 0, readback_data.size());

            ASSERT_EQ(data, readback_data);
        }
    }
}

// This test uses the machine instructions from the header file assembly_programs_for_tests.hpp. How to generate
// this program is explained in the GENERATE_ASSEMBLY_FOR_TESTS.md file.
TEST(TestCluster, DeassertResetBrisc) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    constexpr uint32_t a_variable_value = 0x87654000;
    constexpr uint64_t a_variable_address = 0x10000;
    constexpr uint64_t brisc_code_address = 0;

    uint32_t readback = 0;

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;

    std::vector<uint8_t> zero_data(tensix_l1_size, 0);

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);

            RiscType select_all_tensix_riscv_cores{RiscType::ALL_TENSIX};

            cluster->assert_risc_reset(chip_id, tensix_core, select_all_tensix_riscv_cores);

            cluster->l1_membar(chip_id, {tensix_core});

            // Zero out L1.
            cluster->write_to_device(zero_data.data(), zero_data.size(), chip_id, tensix_core, 0);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(
                simple_brisc_program.data(),
                simple_brisc_program.size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->read_from_device(&readback, chip_id, tensix_core, a_variable_address, sizeof(readback));

            EXPECT_EQ(a_variable_value, readback)
                << "chip_id: " << chip_id << ", x: " << tensix_core.x << ", y: " << tensix_core.y << "\n";
        }
    }
}

TEST(TestCluster, DeassertResetWithCounterBrisc) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    // TODO: remove this check when it is figured out what is happening with Blackhole version of this test.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole architecture, as it seems flaky for Blackhole.";
    }

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;
    std::vector<uint32_t> zero_data(tensix_l1_size / sizeof(uint32_t), 0);

    constexpr uint64_t counter_address = 0x10000;
    constexpr uint64_t brisc_code_address = 0;

    uint32_t first_readback_value = 0;
    uint32_t second_readback_value = 0;

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);

            cluster->write_to_device(zero_data.data(), zero_data.size() * sizeof(uint32_t), chip_id, tensix_core, 0x0);

            cluster->l1_membar(chip_id, {tensix_core});

            RiscType select_all_tensix_riscv_cores{RiscType::ALL_TENSIX};

            cluster->assert_risc_reset(chip_id, tensix_core, select_all_tensix_riscv_cores);

            cluster->write_to_device(
                counter_brisc_program.data(),
                counter_brisc_program.size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            cluster->read_from_device(
                &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

            cluster->read_from_device(
                &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

            // Since we expect BRISC to work and constantly increment counter in L1, we expect values to be different on
            // two reads from device
            EXPECT_NE(second_readback_value, first_readback_value);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->assert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            cluster->read_from_device(
                &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

            cluster->read_from_device(
                &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

            // When the BRISC is in reset state the counter is not incremented in L1, and we expect values to be
            // different on two reads from device
            EXPECT_EQ(second_readback_value, first_readback_value);
        }
    }
}

TEST(TestCluster, SocDescriptorSerialize) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_descriptor = umd_cluster->get_soc_descriptor(chip_id);

        std::filesystem::path file_path = soc_descriptor.serialize_to_file();
        SocDescriptor soc(
            file_path.string(),
            {.noc_translation_enabled = soc_descriptor.noc_translation_enabled,
             .harvesting_masks = soc_descriptor.harvesting_masks});
    }
}

TEST(TestCluster, GetEthernetFirmware) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    // BoardType P100 doesn't have eth cores.
    std::optional<semver_t> eth_version;
    EXPECT_NO_THROW(eth_version = cluster->get_ethernet_firmware_version());
    if (cluster->get_cluster_description()->get_board_type(0) == BoardType::P100) {
        EXPECT_FALSE(eth_version.has_value());
    } else {
        EXPECT_TRUE(eth_version.has_value());
    }
}

TEST(TestCluster, TestMulticastWrite) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const tt_xy_pair grid_size = {8, 8};

    const CoreCoord start_tensix = CoreCoord(0, 0, CoreType::TENSIX, CoordSystem::LOGICAL);
    const CoreCoord end_tensix = CoreCoord(grid_size.x - 1, grid_size.y - 1, CoreType::TENSIX, CoordSystem::LOGICAL);

    const uint64_t address = 0;
    const size_t data_size = 256;
    std::vector<uint8_t> write_data(data_size, 0);
    for (std::size_t i = 0; i < data_size; i++) {
        write_data[i] = (uint8_t)i;
    }

    for (uint32_t x = 0; x < grid_size.x; x++) {
        for (uint32_t y = 0; y < grid_size.y; y++) {
            std::vector<uint8_t> zeros(data_size, 0);
            cluster->write_to_device(
                zeros.data(), zeros.size(), 0, CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL), address);

            std::vector<uint8_t> readback(data_size, 1);
            cluster->read_from_device(
                readback.data(), 0, CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL), address, readback.size());

            EXPECT_EQ(zeros, readback);
        }
    }

    cluster->noc_multicast_write(write_data.data(), write_data.size(), 0, start_tensix, end_tensix, address);

    for (uint32_t x = 0; x < grid_size.x; x++) {
        for (uint32_t y = 0; y < grid_size.y; y++) {
            std::vector<uint8_t> readback(data_size, 0);
            cluster->read_from_device(
                readback.data(), 0, CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL), address, readback.size());

            EXPECT_EQ(write_data, readback);
        }
    }
}

TEST_P(ClusterAssertDeassertRiscsTest, TriscNcriscAssertDeassertTest) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    // TODO: remove this check when it is figured out what is happening with Blackhole version of this test.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole architecture, as it seems flaky for Blackhole.";
    }

    // TODO: remove this check when it is figured out what is happening with llmbox.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::WORMHOLE_B0 &&
        cluster->get_target_device_ids().size() == 8) {
        GTEST_SKIP() << "Skipping test for LLMBox architecture, as it seems flaky.";
    }

    auto get_brisc_configuration_program_for_chip = [](Cluster* cluster,
                                                       ChipId chip_id) -> std::optional<std::array<uint32_t, 14>> {
        switch (cluster->get_cluster_description()->get_arch(chip_id)) {
            case tt::ARCH::WORMHOLE_B0:
                return std::make_optional(wh_brisc_configuration_program);
            case tt::ARCH::BLACKHOLE:
                return std::make_optional(bh_brisc_configuration_program);
            default:
                return std::nullopt;
        }
    };

    const auto& configurations_of_risc_cores = GetParam();

    constexpr uint64_t brisc_code_address = 0;

    uint32_t first_readback_value = 0;
    uint32_t second_readback_value = 0;

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;
    std::vector<uint32_t> zero_data(tensix_l1_size / sizeof(uint32_t), 0);

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        auto brisc_configuration_program = get_brisc_configuration_program_for_chip(cluster.get(), chip_id);

        if (!brisc_configuration_program) {
            GTEST_SKIP() << "Unsupported architecture for deassert test.";
        }

        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        RiscType risc_cores{RiscType::NONE};

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);

            cluster->assert_risc_reset(chip_id, tensix_core, RiscType::ALL_TENSIX);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(zero_data.data(), zero_data.size() * sizeof(uint32_t), chip_id, tensix_core, 0x0);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(
                brisc_configuration_program.value().data(),
                brisc_configuration_program.value().size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto& [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;
                risc_cores = risc_cores | risc_core;

                cluster->write_to_device(
                    code_program.data(), code_program.size() * sizeof(uint32_t), chip_id, tensix_core, code_address);
            }

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, risc_cores);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto& [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;

                cluster->read_from_device(
                    &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

                cluster->read_from_device(
                    &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

                EXPECT_NE(first_readback_value, second_readback_value);
            }

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->assert_risc_reset(chip_id, tensix_core, risc_cores);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;

                cluster->read_from_device(
                    &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

                cluster->read_from_device(
                    &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

                EXPECT_EQ(first_readback_value, second_readback_value);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllTriscNcriscCoreCombinations,
    ClusterAssertDeassertRiscsTest,
    ::testing::ValuesIn(ClusterAssertDeassertRiscsTest::generate_all_risc_cores_combinations()));

TEST(TestCluster, StartDeviceWithValidRiscProgram) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    constexpr uint64_t write_address = 0x1000;

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    test_utils::setup_risc_cores_on_cluster(cluster.get());

    cluster->start_device({});

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    for (auto chip_id : cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        cluster->write_to_device(data.data(), data_size, chip_id, any_core, write_address);

        cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::vector<uint8_t> readback_data(data_size, 0);
        cluster->read_from_device(readback_data.data(), chip_id, any_core, write_address, data_size);

        ASSERT_EQ(data, readback_data);
    }

    cluster->close_device();
}

TEST_P(ClusterReadWriteL1Test, ReadWriteL1) {
    ClusterOptions options = GetParam();
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options);

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (options.chip_type == SIMULATION) {
        cluster->start_device({.init_device = true});
    }

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;

    std::vector<uint8_t> zero_data(tensix_l1_size, 0);
    std::vector<uint8_t> data(tensix_l1_size, 0);
    for (int i = 0; i < tensix_l1_size; i++) {
        data[i] = i % 256;
    }

    // Set elements to 1 since the first readback will be of zero data, so want to confirm that
    // elements actually changed.
    std::vector<uint8_t> readback_data(tensix_l1_size, 1);

    for (auto chip_id : cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        const CoreCoord tensix_core = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX)[0];

        // Zero out L1.
        cluster->write_to_device(zero_data.data(), zero_data.size(), chip_id, tensix_core, 0);

        cluster->wait_for_non_mmio_flush(chip_id);

        cluster->read_from_device(readback_data.data(), chip_id, tensix_core, 0, tensix_l1_size);

        EXPECT_EQ(zero_data, readback_data);

        cluster->write_to_device(data.data(), data.size(), chip_id, tensix_core, 0);

        cluster->wait_for_non_mmio_flush(chip_id);

        cluster->read_from_device(readback_data.data(), chip_id, tensix_core, 0, tensix_l1_size);

        EXPECT_EQ(data, readback_data);
    }
}

// Instantiate the test suite AFTER all TEST_P definitions.
INSTANTIATE_TEST_SUITE_P(
    SiliconAndSimulationCluster,
    ClusterReadWriteL1Test,
    ::testing::ValuesIn(get_cluster_options_for_param_test()),
    [](const ::testing::TestParamInfo<ClusterOptions>& info) {
        switch (info.param.chip_type) {
            case ChipType::SILICON:
                return "Silicon";
            case ChipType::SIMULATION:
                return "Simulation";
            default:
                return "Unknown";
        }
    }

);

/**
 * This is a basic DMA test -- not using the PCIe controller's DMA engine, but
 * rather using the ability of the NOC to access the host system bus via traffic
 * to the PCIe block.
 *
 * sysmem means memory in the host that has been mapped for device access.
 *
 * 1. Fills sysmem with a random pattern.
 * 2. Uses PCIe block to read sysmem at various offsets.
 * 3. Verifies that the data read matches the data written.
 * 4. Zeros out sysmem (via hardware write) at various offsets.
 * 5. Verifies that the offsets have been zeroed from host's perspective.
 */
TEST(TestCluster, SysmemReadWrite) {
    {
        Cluster cluster;
        if (cluster.get_target_device_ids().empty()) {
            GTEST_SKIP() << "No chips present on the system. Skipping test.";
        }
    }
    constexpr size_t ONE_GIG = 1ULL << 30;
    constexpr uint64_t ALIGNMENT = sizeof(uint32_t);
    const bool is_vm = test_utils::is_virtual_machine();
    const bool has_iommu = test_utils::is_iommu_available();

    // 3 for BM with IOMMU to test more of the address space while avoiding
    // the legacy hack for getting to 3.75 on WH.
    // 1 for BM without IOMMU, to avoid making assumptions RE: # of hugepages.
    // 1 for VM because it'll work if vIOMMU; if no vIOMMU it avoids assuming
    // >1 hugepages are available.
    const uint32_t channels = is_vm ? 1 : has_iommu ? 3 : 1;
    Cluster cluster(ClusterOptions{
        .num_host_mem_ch_per_mmio_device = channels,
    });
    constexpr auto mmio_chip_id = 0;
    const auto pci_cores = cluster.get_soc_descriptor(mmio_chip_id).get_cores(CoreType::PCIE);
    const auto pcie_core = pci_cores.at(0);
    const auto base_address = cluster.get_pcie_base_addr_from_device(mmio_chip_id);

    auto random_address_between = [&](uint64_t lo, uint64_t hi) -> uint64_t {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis(lo, hi);
        return dis(gen);
    };

    cluster.get_chip(mmio_chip_id)->start_device();
    // sysmem_manager->pin_or_map_sysmem_to_device();
    // cluster.start_device(device_params{});

    for (uint32_t channel = 0; channel < channels; channel++) {
        uint8_t* sysmem = static_cast<uint8_t*>(cluster.host_dma_address(mmio_chip_id, 0, channel));

        ASSERT_NE(sysmem, nullptr);
        test_utils::fill_with_random_bytes(sysmem, ONE_GIG);

        std::vector<uint64_t> test_offsets = {
            0x0,
            (ONE_GIG / 4) - 0x1000,
            (ONE_GIG / 4) - 0x0004,
            (ONE_GIG / 4),
            (ONE_GIG / 4) + 0x0004,
            (ONE_GIG / 4) + 0x1000,
            (ONE_GIG / 2) - 0x1000,
            (ONE_GIG / 2) - 0x0004,
            (ONE_GIG / 2),
            (ONE_GIG / 2) + 0x0004,
            (ONE_GIG / 2) + 0x1000,
            (ONE_GIG - 0x1000),
            (ONE_GIG - 0x0004),
        };

        for (size_t i = 0; i < 8192; ++i) {
            uint64_t address = random_address_between(0, ONE_GIG);
            test_offsets.push_back(address);
        }

        // Read test - read the sysmem at the various offsets.
        for (uint64_t test_offset : test_offsets) {
            uint64_t aligned_offset = (test_offset / ALIGNMENT) * ALIGNMENT;
            uint64_t device_offset = aligned_offset + channel * ONE_GIG;
            uint64_t noc_addr = base_address + device_offset;
            uint32_t expected = 0;
            uint32_t value = 0;

            std::memcpy(&expected, &sysmem[aligned_offset], sizeof(uint32_t));

            cluster.read_from_device(&value, mmio_chip_id, pcie_core, noc_addr, sizeof(uint32_t));

            if (value != expected) {
                std::stringstream error_msg;
                error_msg << "Sysmem read mismatch at channel " << channel << ", offset 0x" << std::hex
                          << aligned_offset << std::dec << " (NOC addr 0x" << std::hex << noc_addr << std::dec << ")"
                          << "\n  Configuration: " << (is_vm ? "VM" : "Bare Metal")
                          << ", IOMMU: " << (has_iommu ? "Enabled" : "Disabled") << ", Channels: " << channels
                          << "\n  Expected: 0x" << std::hex << expected << ", Got: 0x" << value << std::dec;

                if (is_vm && has_iommu) {
                    error_msg << "\n"
                              << "\n  - VM with IOMMU detected: This is likely a DMA mapping limit issue"
                              << "\n  - FIX: On the HOST machine, add this kernel boot parameter:"
                              << "\n      vfio_iommu_type1.dma_entry_limit=4294967295"
                              << "\n  - After adding the parameter, reboot the HOST (not just the VM)"
                              << "\n  - Check host dmesg for IO page faults"
                              << "\n  - Failure at offset >= 255MB strongly indicates dma_entry_limit issue";
                }

                FAIL() << error_msg.str();
            }
        }

        // Write test - zero out the sysmem at the various offsets.
        for (uint64_t test_offset : test_offsets) {
            uint64_t aligned_offset = (test_offset / ALIGNMENT) * ALIGNMENT;
            uint64_t device_offset = aligned_offset + channel * ONE_GIG;
            uint64_t noc_addr = base_address + device_offset;
            uint32_t value = 0;
            cluster.write_to_device(&value, sizeof(uint32_t), mmio_chip_id, pcie_core, noc_addr);
            cluster.read_from_device(&value, mmio_chip_id, pcie_core, noc_addr, sizeof(uint32_t));
        }

        // Write test verification - read the sysmem at the various offsets and verify that each has been zeroed.
        for (uint64_t test_offset : test_offsets) {
            uint64_t aligned_offset = (test_offset / ALIGNMENT) * ALIGNMENT;
            uint64_t device_offset = aligned_offset + channel * ONE_GIG;
            uint64_t noc_addr = base_address + device_offset;
            uint32_t value = 0xffffffff;
            std::memcpy(&value, &sysmem[aligned_offset], sizeof(uint32_t));
            EXPECT_EQ(value, 0);
        }
    }
}

TEST(TestCluster, RegReadWrite) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const CoreCoord tensix_core = cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX)[0];

    const size_t l1_size = cluster->get_soc_descriptor(0).worker_l1_size;

    std::vector<uint8_t> zeros(l1_size, 0);

    cluster->write_to_device(zeros.data(), zeros.size(), 0, tensix_core, 0);

    std::vector<uint8_t> readback_vec(l1_size, 1);
    cluster->read_from_device(readback_vec.data(), 0, tensix_core, 0, readback_vec.size());

    EXPECT_EQ(zeros, readback_vec);

    size_t addr = 0;
    uint32_t value = 0;
    while (addr < l1_size) {
        cluster->write_to_device_reg(&value, sizeof(value), 0, tensix_core, addr);

        if (addr + 4 < l1_size) {
            // Write some garbage after the written register to ensure that
            // readback only reads the intended register.
            uint32_t write_value = 0xDEADBEEF;
            cluster->write_to_device_reg(&write_value, sizeof(write_value), 0, tensix_core, addr + 4);
        }

        uint32_t readback_value = 0;
        cluster->read_from_device_reg(&readback_value, 0, tensix_core, addr, sizeof(readback_value));

        EXPECT_EQ(value, readback_value);

        if (addr + 4 < l1_size) {
            // Ensure that the garbage value is still there.
            uint32_t readback = 0;
            cluster->read_from_device_reg(&readback, 0, tensix_core, addr + 4, sizeof(readback));
            EXPECT_EQ(0xDEADBEEF, readback);
        }

        value += 4;
        addr += 4;
    }
}

TEST(TestCluster, WriteDataReadReg) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    const CoreCoord tensix_core = cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX)[0];

    const size_t l1_size = cluster->get_soc_descriptor(0).worker_l1_size;

    std::vector<uint32_t> write_data_l1(l1_size / 4, 0);
    for (size_t i = 0; i < l1_size / 4; i++) {
        write_data_l1[i] = i;
    }

    cluster->write_to_device(write_data_l1.data(), write_data_l1.size() * sizeof(uint32_t), 0, tensix_core, 0);

    std::vector<uint32_t> readback_vec(l1_size / 4, 0);
    cluster->read_from_device(readback_vec.data(), 0, tensix_core, 0, readback_vec.size() * sizeof(uint32_t));

    EXPECT_EQ(write_data_l1, readback_vec);

    for (size_t i = 0; i < l1_size / 4; i++) {
        uint32_t readback_value = 0;
        cluster->read_from_device_reg(&readback_value, 0, tensix_core, i * 4, sizeof(readback_value));

        EXPECT_EQ(write_data_l1[i], readback_value);
    }
}

TEST(TestCluster, EriscFirmwareHashCheck) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    auto eth_fw_version = cluster->get_ethernet_firmware_version();
    if (!eth_fw_version.has_value()) {
        GTEST_SKIP() << "No ETH cores in Cluster. Skipping test.";
    }
    auto first_chip = cluster->get_chip(*cluster->get_target_device_ids().begin());
    auto first_eth_core = first_chip->get_soc_descriptor().get_cores(tt::CoreType::ETH)[0];

    const std::unordered_map<semver_t, erisc_firmware::HashedAddressRange>* eth_fw_hashes = nullptr;
    switch (first_chip->get_tt_device()->get_arch()) {
        case ARCH::WORMHOLE_B0:
            eth_fw_hashes = &erisc_firmware::WH_ERISC_FW_HASHES;
            break;
        case ARCH::BLACKHOLE:
            eth_fw_hashes = &erisc_firmware::BH_ERISC_FW_HASHES;
            break;
        default:
            GTEST_SKIP() << "Unsupported architecture for test.";
    }

    // Check hash without changes, should pass.
    std::cout << "Checking ETH FW without changes." << std::endl;
    auto result = verify_eth_fw_integrity(first_chip->get_tt_device(), first_eth_core, eth_fw_version.value());
    if (!result.has_value()) {
        GTEST_SKIP() << "No known hash for found ETH firmware version.";
    }
    ASSERT_EQ(result, true);
    std::cout << "Passed hash check." << std::endl;

    // Corrupt a part of ERISC FW code.
    std::cout << fmt::format("Corrupting ETH core {} firmware.", first_eth_core.str()) << std::endl;
    const erisc_firmware::HashedAddressRange& range = eth_fw_hashes->find(eth_fw_version.value())->second;
    size_t start_addr = range.start_address;
    std::vector<uint32_t> ebreak_instr_vector(32, 0x00100073);

    first_chip->assert_risc_reset(RiscType::ALL);
    first_chip->write_to_device(first_eth_core, ebreak_instr_vector.data(), start_addr, ebreak_instr_vector.size());
    first_chip->l1_membar(std::unordered_set<CoreCoord>{first_eth_core});
    first_chip->deassert_risc_reset(RiscType::ALL, false);

    result = verify_eth_fw_integrity(first_chip->get_tt_device(), first_eth_core, eth_fw_version.value());
    EXPECT_EQ(result.value(), false);
    std::cout << "Passed hash check." << std::endl;

    // Revert ERISC FW state with warm reset.
    if (is_galaxy_configuration(cluster.get())) {
        WarmReset::ubb_warm_reset();
    } else {
        WarmReset::warm_reset();
    }
    std::cout << "Completed warm reset." << std::endl;
}
