// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <gtest/gtest.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>  // for std::getenv
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "fmt/xchar.h"
#include "test_utils/assembly_programs_for_tests.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "tests/test_utils/test_api_common.h"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/chip/local_chip.h"
#include "umd/device/chip/mock_chip.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"
#include "umd/device/tt_core_coordinates.h"
#include "umd/device/tt_silicon_driver_common.hpp"
#include "umd/device/types/arch.h"
#include "umd/device/types/cluster_descriptor_types.h"
#include "umd/device/wormhole_implementation.h"

// TODO: obviously we need some other way to set this up
#include "noc/noc_parameters.h"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy

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

        // Make sure that Cluster construction is without exceptions.
        // TODO: add cluster descriptors for expected topologies, compare cluster desc against expected desc.
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{
            .pci_target_devices = target_pci_device_ids,
        });

        if (!target_pci_device_ids.empty()) {
            // If target_pci_device_ids is empty, then full cluster will be created, so skip the check.
            // Check that the cluster has the expected number of chips.
            auto actual_pci_device_ids = cluster->get_target_mmio_device_ids();
            EXPECT_EQ(actual_pci_device_ids.size(), target_pci_device_ids.size());
            // Always expect logical id 0 to exist, that's the way filtering by pci ids work.
            EXPECT_TRUE(actual_pci_device_ids.find(0) != actual_pci_device_ids.end());
        }

        std::string value = test_utils::convert_to_comma_separated_string(target_pci_device_ids);

        if (setenv(TT_VISIBLE_DEVICES_ENV.data(), value.c_str(), 1) != 0) {
            ASSERT_TRUE(false) << "Failed to unset environment variable.";
        }

        // Make sure that Cluster construction is without exceptions.
        // TODO: add cluster descriptors for expected topologies, compare cluster desc against expected desc.
        std::unique_ptr<Cluster> cluster_env_var = std::make_unique<Cluster>(ClusterOptions{
            .pci_target_devices = {},
        });

        if (!target_pci_device_ids.empty()) {
            // If target_pci_device_ids is empty, then full cluster will be created, so skip the check.
            // Check that the cluster has the expected number of chips.
            auto actual_pci_device_ids = cluster->get_target_mmio_device_ids();
            EXPECT_EQ(actual_pci_device_ids.size(), target_pci_device_ids.size());
            // Always expect logical id 0 to exist, that's the way filtering by pci ids work.
            EXPECT_TRUE(actual_pci_device_ids.find(0) != actual_pci_device_ids.end());
        }

        if (unsetenv(TT_VISIBLE_DEVICES_ENV.data()) != 0) {
            ASSERT_TRUE(false) << "Failed to unset environment variable.";
        }
    }
}

TEST(ApiClusterTest, OpenClusterByLogicalID) {
    // First, pregenerate a cluster descriptor and save it to a file.
    // This will run topology discovery and touch all the devices.
    std::filesystem::path cluster_path = Cluster::create_cluster_descriptor()->serialize_to_file();

    // Now, the user can create the cluster descriptor without touching the devices.
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(cluster_path);
    // You can test the cluster descriptor here to see if the topology matched the one you'd expect.
    // For example, you can check if the number of chips is correct, or number of pci devices, or nature of eth
    // connections.
    std::unordered_set<chip_id_t> all_chips = cluster_desc->get_all_chips();
    std::unordered_map<chip_id_t, chip_id_t> chips_with_pcie = cluster_desc->get_chips_with_mmio();
    auto eth_connections = cluster_desc->get_ethernet_connections();

    if (all_chips.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    // Now we can choose which chips to open. This can be hardcoded if you already have expected topology.
    // The first cluster will open the first chip only, and the second cluster will open the rest of them.
    chip_id_t first_chip_only = chips_with_pcie.begin()->first;
    std::unique_ptr<Cluster> umd_cluster1 = std::make_unique<Cluster>(ClusterOptions{
        .target_devices = {first_chip_only},
        .cluster_descriptor = cluster_desc.get(),
    });

    auto chips1 = umd_cluster1->get_target_device_ids();
    EXPECT_EQ(chips1.size(), 1);
    EXPECT_EQ(*chips1.begin(), first_chip_only);

    std::unordered_set<chip_id_t> other_chips;
    for (auto chip : all_chips) {
        // Skip the first chip, but also skip all remote chips so that we don't accidentally hit the one tied to the
        // first local chip.
        if (chip != first_chip_only && cluster_desc->is_chip_mmio_capable(chip)) {
            other_chips.insert(chip);
        }
    }
    // Continue the test only if there there is more than one card in the system
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
        std::string sdesc_path = tt_SocDescriptor::get_soc_descriptor_path(device_arch);
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

    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(cluster_path1);
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

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

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
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size);

        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiClusterTest, RemoteFlush) {
    std::unique_ptr<Cluster> umd_cluster = std::make_unique<Cluster>();

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_remote_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

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

    const tt_ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

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
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);

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

    tt_device_params default_params;
    cluster->start_device(default_params);

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = zeros;

    static const uint32_t num_loops = 100;

    for (const chip_id_t chip : cluster->get_target_device_ids()) {
        // Just make sure to skip L1_BARRIER_BASE
        std::uint32_t address = 0x100;
        // Write to each core a 100 times at different statically mapped addresses
        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip);
        std::vector<CoreCoord> tensix_cores = soc_desc.get_cores(CoreType::TENSIX);
        for (int loop = 0; loop < num_loops; loop++) {
            for (auto& core : tensix_cores) {
                cluster->write_to_device(
                    vector_to_write.data(), vector_to_write.size() * sizeof(std::uint32_t), chip, core, address);

                // Barrier to ensure that all writes over ethernet were commited
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

    for (chip_id_t chip : umd_cluster->get_target_device_ids()) {
        std::cout << "Chip " << chip << std::endl;

        const tt_SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip);

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

    tt_ClusterDescriptor* cluster_desc = cluster->get_cluster_description();

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

    auto get_expected_clock_val = [&cluster](chip_id_t chip_id, bool busy) {
        tt::ARCH arch = cluster->get_cluster_description()->get_arch(chip_id);
        if (arch == tt::ARCH::WORMHOLE_B0) {
            return busy ? wormhole::AICLK_BUSY_VAL : wormhole::AICLK_IDLE_VAL;
        } else if (arch == tt::ARCH::BLACKHOLE) {
            return busy ? blackhole::AICLK_BUSY_VAL : blackhole::AICLK_IDLE_VAL;
        }
        return 0u;
    };

    cluster->set_power_state(tt_DevicePowerState::BUSY);

    auto clocks_busy = cluster->get_clocks();
    for (auto& clock : clocks_busy) {
        // TODO #781: Figure out a proper mechanism to detect the right value. For now just check that Busy value is
        // larger than Idle value.
        EXPECT_GT(clock.second, get_expected_clock_val(clock.first, false));
    }

    cluster->set_power_state(tt_DevicePowerState::LONG_IDLE);

    auto clocks_idle = cluster->get_clocks();
    for (auto& clock : clocks_idle) {
        EXPECT_EQ(clock.second, get_expected_clock_val(clock.first, false));
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
        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);

            TensixSoftResetOptions select_all_tensix_riscv_cores{TENSIX_ASSERT_SOFT_RESET};

            chip->set_tensix_risc_reset(
                cluster->get_soc_descriptor(chip_id).translate_coord_to(tensix_core, CoordSystem::VIRTUAL),
                select_all_tensix_riscv_cores);

            cluster->l1_membar(chip_id, {tensix_core});

            // Zero out L1.
            cluster->write_to_device(zero_data.data(), zero_data.size(), chip_id, tensix_core, 0);

            cluster->write_to_device(
                simple_brisc_program.data(),
                simple_brisc_program.size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            chip->unset_tensix_risc_reset(
                cluster->get_soc_descriptor(chip_id).translate_coord_to(tensix_core, CoordSystem::VIRTUAL),
                TensixSoftResetOptions::BRISC);

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
        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);
            auto core = cluster->get_soc_descriptor(chip_id).translate_coord_to(tensix_core, CoordSystem::VIRTUAL);

            cluster->write_to_device(zero_data.data(), zero_data.size() * sizeof(uint32_t), chip_id, tensix_core, 0x0);

            cluster->l1_membar(chip_id, {tensix_core});

            TensixSoftResetOptions select_all_tensix_riscv_cores{TENSIX_ASSERT_SOFT_RESET};

            chip->set_tensix_risc_reset(core, select_all_tensix_riscv_cores);

            cluster->write_to_device(
                counter_brisc_program.data(),
                counter_brisc_program.size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            chip->unset_tensix_risc_reset(core, TensixSoftResetOptions::BRISC);

            cluster->read_from_device(
                &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

            cluster->read_from_device(
                &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

            // Since we expect BRISC to work and constantly increment counter in L1, we expect values to be different on
            // two reads from device
            EXPECT_NE(second_readback_value, first_readback_value);

            cluster->l1_membar(chip_id, {tensix_core});

            chip->set_tensix_risc_reset(core, TensixSoftResetOptions::BRISC);

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

TEST_P(ClusterAssertDeassertRiscsTest, TriscNcriscAssertDeassertTest) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    // TODO: remove this check when it is figured out what is happening with Blackhole version of this test.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole architecture, as it seems flaky for Blackhole.";
    }

    auto get_brisc_configuration_program_for_chip = [](Cluster* cluster,
                                                       chip_id_t chip_id) -> std::optional<std::array<uint32_t, 14>> {
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

        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        TensixSoftResetOptions risc_cores{TensixSoftResetOptions::NONE};

        for (const CoreCoord& tensix_core : tensix_cores) {
            auto chip = cluster->get_chip(chip_id);
            auto core = cluster->get_soc_descriptor(chip_id).translate_coord_to(tensix_core, CoordSystem::VIRTUAL);

            chip->set_tensix_risc_reset(core, TENSIX_ASSERT_SOFT_RESET);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(zero_data.data(), zero_data.size() * sizeof(uint32_t), chip_id, tensix_core, 0x0);

            cluster->write_to_device(
                brisc_configuration_program.value().data(),
                brisc_configuration_program.value().size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            chip->unset_tensix_risc_reset(core, TensixSoftResetOptions::BRISC);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto& [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;
                risc_cores = risc_cores | risc_core;

                cluster->write_to_device(
                    code_program.data(), code_program.size() * sizeof(uint32_t), chip_id, tensix_core, code_address);
            }

            cluster->l1_membar(chip_id, {tensix_core});

            chip->unset_tensix_risc_reset(core, risc_cores);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto& [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;

                cluster->read_from_device(
                    &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

                cluster->read_from_device(
                    &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

                EXPECT_NE(first_readback_value, second_readback_value);
            }

            cluster->l1_membar(chip_id, {tensix_core});

            chip->set_tensix_risc_reset(core, risc_cores);

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

TEST_P(ClusterReadWriteL1Test, ReadWriteL1) {
    ClusterOptions options = GetParam();
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options);

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }
    if (options.chip_type == SIMULATION) {
        tt_device_params device_params;
        device_params.init_device = true;
        cluster->start_device(device_params);
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
        const tt_SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        std::vector<CoreCoord> tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
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
}

// Instantiate the test suite AFTER all TEST_P definitions
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
