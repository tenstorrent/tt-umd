// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>  // for std::getenv
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "test_utils/setup_risc_cores.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/warm_reset.hpp"
#include "utils.hpp"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy.
// They also support TTSim simulation when TT_UMD_SIMULATOR env var is set.

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

class TestDeviceIOFixture : public ::testing::TestWithParam<CoreType> {
protected:
    static constexpr const char* TT_UMD_SIMULATOR_ENV = "TT_UMD_SIMULATOR";

    // Creates a Cluster with the given options, overriding chip_type/target_devices/simulator_directory
    // to use simulation when TT_UMD_SIMULATOR is set.
    std::unique_ptr<Cluster> make_cluster(ClusterOptions options = {}) {
        if (const char* sim_path = std::getenv(TT_UMD_SIMULATOR_ENV)) {
            options.chip_type = ChipType::SIMULATION;
            options.target_devices = {0};
            options.simulator_directory = std::filesystem::path(sim_path);
        }
        return std::make_unique<Cluster>(options);
    }

    bool is_simulation() const { return std::getenv(TT_UMD_SIMULATOR_ENV) != nullptr; }
};

TEST_P(TestDeviceIOFixture, SimpleIOAllTargets) {
    const CoreType core_type = GetParam();
    std::unique_ptr<Cluster> umd_cluster = make_cluster();

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
        const auto& cores = soc_desc.get_cores(core_type);

        const CoreCoord& any_core = cores[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);
        const auto& cores = soc_desc.get_cores(core_type);

        const CoreCoord& any_core = cores[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size);

        ASSERT_EQ(data, readback_data);
    }
}

TEST_P(TestDeviceIOFixture, RemoteFlush) {
    const CoreType core_type = GetParam();
    std::unique_ptr<Cluster> umd_cluster = make_cluster();

    const ClusterDescriptor* cluster_desc = umd_cluster->get_cluster_description();

    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);

    // Setup memory barrier addresses.
    // Some default values are set during construction of UMD, but you can override them.
    umd_cluster->set_barrier_address_params({L1_BARRIER_BASE, ETH_BARRIER_BASE, DRAM_BARRIER_BASE});

    for (auto chip_id : umd_cluster->get_target_remote_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);
        const auto& cores = soc_desc.get_cores(core_type);

        const CoreCoord& any_core = cores[0];

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

TEST_P(TestDeviceIOFixture, SimpleIOSpecificDevices) {
    const CoreType core_type = GetParam();
    std::unique_ptr<Cluster> umd_cluster = make_cluster(ClusterOptions{
        .target_devices = {0},
    });

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
        const auto& cores = soc_desc.get_cores(core_type);

        const CoreCoord& any_core = cores[0];

        std::cout << "Writing to chip " << chip_id << " core " << any_core.str() << std::endl;

        umd_cluster->write_to_device(data.data(), data_size, chip_id, any_core, 0);

        umd_cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : umd_cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = umd_cluster->get_soc_descriptor(chip_id);
        const auto& cores = soc_desc.get_cores(core_type);

        const CoreCoord& any_core = cores[0];

        std::cout << "Reading from chip " << chip_id << " core " << any_core.str() << std::endl;

        std::vector<uint8_t> readback_data(data_size, 0);
        umd_cluster->read_from_device(readback_data.data(), chip_id, any_core, 0, data_size);

        ASSERT_EQ(data, readback_data);
    }
}

TEST_P(TestDeviceIOFixture, DynamicTLB_RW) {
    // Don't use any static TLBs in this test. All writes go through a dynamic TLB that needs
    // to be reconfigured for each transaction
    const CoreType core_type = GetParam();

    std::unique_ptr<Cluster> cluster = make_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> zeros = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> readback_vec = zeros;

    static const uint32_t num_loops = 100;

    for (const ChipId chip : cluster->get_target_device_ids()) {
        // Just make sure to skip L1_BARRIER_BASE.
        std::uint32_t address = 0x100;
        // Write to each core a 100 times at different statically mapped addresses.
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip);
        const auto& cores = soc_desc.get_cores(core_type);

        for (int loop = 0; loop < num_loops; loop++) {
            for (const auto& core : cores) {
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

TEST_F(TestDeviceIOFixture, TestMulticastWrite) {
    std::unique_ptr<Cluster> cluster = make_cluster();

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

TEST_F(TestDeviceIOFixture, TestDmaMulticastWrite) {
    std::unique_ptr<Cluster> cluster = make_cluster();

    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "DMA multicast write is not supported on Blackhole architecture.";
    }

    if (is_simulation()) {
        GTEST_SKIP() << "DMA multicast write is not supported in simulation.";
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

    cluster->dma_multicast_write(write_data.data(), write_data.size(), 0, start_tensix, end_tensix, address);

    for (uint32_t x = 0; x < grid_size.x; x++) {
        for (uint32_t y = 0; y < grid_size.y; y++) {
            std::vector<uint8_t> readback(data_size, 0);
            cluster->read_from_device(
                readback.data(), 0, CoreCoord(x, y, CoreType::TENSIX, CoordSystem::LOGICAL), address, readback.size());

            EXPECT_EQ(write_data, readback);
        }
    }
}

TEST_P(ClusterReadWriteL1Test, ReadWriteL1) {
    ClusterOptions options = GetParam();
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options);

    if (options.chip_type == ChipType::SIMULATION) {
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
    });

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
TEST_F(TestDeviceIOFixture, SysmemReadWrite) {
    constexpr size_t ONE_GIG = 1ULL << 30;
    constexpr uint64_t ALIGNMENT = sizeof(uint32_t);

    uint32_t channels;
    uint32_t channels_to_test;
    if (is_simulation()) {
        channels = 4;
        channels_to_test = 1;
    } else {
        const bool is_vm = test_utils::is_virtual_machine();
        const bool has_iommu = test_utils::is_iommu_available();
        channels = is_vm ? 1 : has_iommu ? 3 : 1;
        channels_to_test = channels;
    }

    std::unique_ptr<Cluster> cluster = make_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = channels});
    constexpr auto mmio_chip_id = 0;
    const auto pci_cores = cluster->get_soc_descriptor(mmio_chip_id).get_cores(CoreType::PCIE);
    const auto pcie_core = pci_cores.at(0);
    const auto base_address = cluster->get_pcie_base_addr_from_device(mmio_chip_id);

    auto random_address_between = [&](uint64_t lo, uint64_t hi) -> uint64_t {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis(lo, hi);
        return dis(gen);
    };

    if (!is_simulation()) {
        test_utils::safe_test_cluster_start(cluster.get());
    }

    for (uint32_t channel = 0; channel < channels_to_test; channel++) {
        uint8_t* sysmem = static_cast<uint8_t*>(cluster->host_dma_address(mmio_chip_id, 0, channel));

        ASSERT_NE(sysmem, nullptr);

        if (is_simulation()) {
            for (size_t i = 0; i < ONE_GIG; i++) {
                sysmem[i] = i % 256;
            }
        } else {
            test_utils::fill_with_random_bytes(sysmem, ONE_GIG);
        }

        std::vector<uint64_t> test_offsets;
        if (is_simulation()) {
            test_offsets = {0x0};
        } else {
            test_offsets = {
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
        }

        // Read test - read the sysmem at the various offsets.
        for (uint64_t test_offset : test_offsets) {
            uint64_t aligned_offset = (test_offset / ALIGNMENT) * ALIGNMENT;
            uint64_t device_offset = aligned_offset + channel * ONE_GIG;
            uint64_t noc_addr = base_address + device_offset;
            uint32_t expected = 0;
            uint32_t value = 0;

            std::memcpy(&expected, &sysmem[aligned_offset], sizeof(uint32_t));

            cluster->read_from_device(&value, mmio_chip_id, pcie_core, noc_addr, sizeof(uint32_t));

            if (!is_simulation() && value != expected) {
                std::stringstream error_msg;
                const bool is_vm = test_utils::is_virtual_machine();
                const bool has_iommu = test_utils::is_iommu_available();
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
            } else {
                EXPECT_EQ(value, expected)
                    << "Sysmem read mismatch at channel " << channel << ", offset 0x" << std::hex << aligned_offset
                    << std::dec << " (NOC addr 0x" << std::hex << noc_addr << std::dec << ")\n"
                    << "Expected: 0x" << std::hex << expected << ", Got: 0x" << value << std::dec;
            }
        }

        // Write test - zero out the sysmem at the various offsets.
        for (uint64_t test_offset : test_offsets) {
            uint64_t aligned_offset = (test_offset / ALIGNMENT) * ALIGNMENT;
            uint64_t device_offset = aligned_offset + channel * ONE_GIG;
            uint64_t noc_addr = base_address + device_offset;
            uint32_t value = 0;
            cluster->write_to_device(&value, sizeof(uint32_t), mmio_chip_id, pcie_core, noc_addr);
            cluster->read_from_device(&value, mmio_chip_id, pcie_core, noc_addr, sizeof(uint32_t));
        }

        // Write test verification - read the sysmem at the various offsets and verify that each has been zeroed.
        for (uint64_t test_offset : test_offsets) {
            uint64_t aligned_offset = (test_offset / ALIGNMENT) * ALIGNMENT;
            uint32_t value = 0xffffffff;
            std::memcpy(&value, &sysmem[aligned_offset], sizeof(uint32_t));
            EXPECT_EQ(value, 0);
        }
    }
}

TEST_F(TestDeviceIOFixture, RegReadWrite) {
    std::unique_ptr<Cluster> cluster = make_cluster();

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

TEST_F(TestDeviceIOFixture, WriteDataReadReg) {
    std::unique_ptr<Cluster> cluster = make_cluster();

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

INSTANTIATE_TEST_SUITE_P(
    CoreTypes,
    TestDeviceIOFixture,
    ::testing::Values(CoreType::TENSIX, CoreType::DRAM),
    [](const ::testing::TestParamInfo<CoreType>& info) { return std::string(to_str(info.param)); });

/**
 * Helper that reads data from a device core using the appropriate mechanism for the
 * current architecture. On Wormhole B0, PCIe DMA reads are required/preferred, so
 * dma_read_from_device is used. On other architectures, the standard read_from_device
 * path is used instead.
 */
void read_data_based_on_architecture(Cluster& cluster, CoreCoord core, void* mem_ptr, uint64_t address, size_t size) {
    if (cluster.get_tt_device(0)->get_arch() == tt::ARCH::WORMHOLE_B0) {
        cluster.dma_read_from_device(mem_ptr, size, 0, core, address);
    } else {
        cluster.read_from_device(mem_ptr, 0, core, address, size);
    }
}

/**
 * Test the PCIe DMA controller by using it to write random fixed-size patterns
 * to 0x0 in several DRAM cores, then reading them back and verifying.
 */
TEST(TestDeviceIO, DMA1) {
    const ChipId chip = 0;
    Cluster cluster;

    auto& soc_descriptor = cluster.get_soc_descriptor(chip);
    size_t dram_count = soc_descriptor.get_num_dram_channels();
    std::vector<CoreCoord> dram_cores;
    dram_cores.reserve(dram_count);
    for (size_t i = 0; i < dram_count; ++i) {
        dram_cores.push_back(soc_descriptor.get_dram_core_for_channel(i, 0, CoordSystem::NOC0));
    }

    // 16.5 MiB: Larger than the largest WH TLB window; this forces chunking
    // and TLB reassignment.
    size_t buf_size = 0x1080000;

    // Keep track of the patterns we wrote to DRAM so we can verify them later.
    std::vector<std::vector<uint8_t>> patterns;

    // First, write a different pattern to each of the DRAM cores.
    for (auto core : dram_cores) {
        std::vector<uint8_t> pattern(buf_size);
        test_utils::fill_with_random_bytes(pattern.data(), pattern.size());

        cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, core, 0x0);

        patterns.push_back(pattern);
    }

    // Now, read back the patterns we wrote to DRAM and verify them.
    for (size_t i = 0; i < dram_cores.size(); ++i) {
        auto core = dram_cores[i];
        std::vector<uint8_t> readback(buf_size, 0x0);

        read_data_based_on_architecture(cluster, core, readback.data(), 0x0, readback.size());

        EXPECT_EQ(patterns[i], readback) << "Mismatch for core " << core.str() << " addr=0x0"
                                         << " size=" << std::dec << readback.size();
    }
}

/**
 * Test the PCIe DMA controller by using it to write random patterns of random
 * sizes to address 0x0 in a single DRAM core, then reading them back and
 * verifying.  Sizes are constrained to be between 4 bytes and 32 MiB, and are
 * aligned to 4 bytes.  Also tested is the case where the write is done using
 * MMIO instead of DMA.
 */
TEST(TestDeviceIO, DMA2) {
    const ChipId chip = 0;
    Cluster cluster;

    auto& soc_descriptor = cluster.get_soc_descriptor(chip);
    size_t dram_count = 1;
    std::vector<CoreCoord> dram_cores;
    dram_cores.reserve(dram_count);
    for (size_t i = 0; i < dram_count; ++i) {
        dram_cores.push_back(soc_descriptor.get_dram_core_for_channel(i, 0, CoordSystem::NOC0));
    }

    // Constraints for random size generation.
    const size_t MIN_BUF_SIZE = 4;
    const size_t MAX_BUF_SIZE = 0x2000000;

    // Setup random number generation.
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<size_t> size_dist(MIN_BUF_SIZE, MAX_BUF_SIZE);

    // Structure to keep track of the operations.
    struct DmaOpInfo {
        CoreCoord core;
        uint64_t address;
        std::vector<uint8_t> data;  // Store the actual data written for verification.
    };

    const size_t ITERATIONS = 25;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        std::vector<DmaOpInfo> write_ops;
        write_ops.reserve(dram_cores.size());

        // First, write a different random pattern to a random address on each DRAM core.
        for (const auto& core : dram_cores) {
            // Generate random size and address.
            size_t size = size_dist(rng) & ~0x3ULL;
            uint64_t addr = 0;

            // Generate a random pattern of the specified size.
            std::vector<uint8_t> pattern(size);
            test_utils::fill_with_random_bytes(pattern.data(), pattern.size());

            // Perform the DMA write.
            cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, core, addr);

            // Store the operation details for verification.
            write_ops.push_back({core, addr, pattern});
        }

        // Now, read back the patterns we wrote to DRAM and verify them.
        for (const auto& op : write_ops) {
            std::vector<uint8_t> readback(op.data.size());

            read_data_based_on_architecture(cluster, op.core, readback.data(), op.address, readback.size());

            // Verify the data.
            EXPECT_EQ(op.data, readback) << "Mismatch for core " << op.core.str() << " addr=0x" << std::hex
                                         << op.address << " size=" << std::dec << op.data.size();
        }
    }

    // Do it again but use MMIO writes to the DRAM cores instead of DMA.
    // DMA is still used for readback.  The inverse of this test (DMA for write;
    // MMIO for read) is omitted because of how slow MMIO reads are.
    for (size_t i = 0; i < ITERATIONS; ++i) {
        std::vector<DmaOpInfo> write_ops;
        write_ops.reserve(dram_cores.size());

        // First, write a different random pattern to a random address on each DRAM core.
        for (const auto& dram_core : dram_cores) {
            // Generate random size and address.
            size_t size = size_dist(rng) & ~0x3ULL;
            uint64_t addr = 0;

            // Generate a random pattern of the specified size.
            std::vector<uint8_t> pattern(size);
            test_utils::fill_with_random_bytes(pattern.data(), pattern.size());

            // Perform the DMA write.
            cluster.write_to_device(pattern.data(), pattern.size(), chip, dram_core, addr);

            // Store the operation details for verification.
            write_ops.push_back({dram_core, addr, pattern});
        }

        // Add a membar on all dram_cores to ensure the write is completed before reading back.
        // But before that we must set a dram membar which is not conflicting with the write and read we're doing.
        // The DRAM buffer written will always start at 0x0, and we can set barrier after the maximum buffer size.
        auto default_l1_address_params =
            cluster.get_tt_device(chip)->get_architecture_implementation()->get_l1_address_params();
        cluster.set_barrier_address_params(
            {default_l1_address_params.tensix_l1_barrier_base,
             default_l1_address_params.eth_l1_barrier_base,
             MAX_BUF_SIZE});
        cluster.dram_membar(chip);

        // Now, read back the patterns we wrote to DRAM and verify them.
        for (const auto& op : write_ops) {
            std::vector<uint8_t> readback(op.data.size());

            read_data_based_on_architecture(cluster, op.core, readback.data(), op.address, readback.size());

            // Verify the data.
            EXPECT_EQ(op.data, readback) << "Mismatch for core " << op.core.str() << " addr=0x" << std::hex
                                         << op.address << " size=" << std::dec << op.data.size();
        }
    }
}

/**
 * Test the PCIe DMA controller by using it to write and read fixed-size patterns
 * to and from a single ETH core at device address 254304 (0x3E160), then verify
 * that the data read back matches what was written.
 */
TEST(TestDeviceIO, DMA3) {
    const ChipId chip = 0;
    Cluster cluster;

    CoreCoord eth_core = cluster.get_soc_descriptor(chip).get_cores(CoreType::ETH)[0];

    size_t buf_size = 768;

    std::vector<uint8_t> zeros(buf_size, 1);
    cluster.write_to_device(zeros.data(), zeros.size(), chip, eth_core, 254304);
    std::vector<uint8_t> readback_zeros(buf_size, 0xFF);
    cluster.read_from_device(readback_zeros.data(), chip, eth_core, 254304, readback_zeros.size());
    EXPECT_EQ(zeros, readback_zeros) << "Mismatch zeros for core " << eth_core.str() << " addr=0x0"
                                     << " size=" << std::dec << readback_zeros.size();

    std::vector<uint8_t> pattern(buf_size, 0);
    for (size_t i = 0; i < buf_size; ++i) {
        pattern[i] = i % 256;
    }

    cluster.dma_write_to_device(pattern.data(), pattern.size(), chip, eth_core, 254304);

    std::vector<uint8_t> readback(buf_size, 1);
    cluster.read_from_device(readback.data(), chip, eth_core, 254304, readback.size());

    EXPECT_EQ(pattern, readback) << "Mismatch for core " << eth_core.str() << " addr=0x0"
                                 << " size=" << std::dec << readback.size();
}
