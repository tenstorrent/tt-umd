// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/arc/spi_tt_device.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "utils.hpp"

using namespace tt::umd;

// SPI Address Constants.
static constexpr uint32_t SPI_BOARD_INFO_ADDR = 0x20108;
static constexpr uint32_t SPI_SPARE_AREA_ADDR = 0x20134;

// Helper function to set up devices for SPI testing.
std::unordered_map<ChipId, std::unique_ptr<TTDevice>> setup_spi_test_devices() {
    auto [cluster_desc, _] = TopologyDiscovery::discover({});
    std::unordered_map<ChipId, std::unique_ptr<TTDevice>> tt_devices;

    for (ChipId chip_id : cluster_desc->get_chips_local_first(cluster_desc->get_all_chips())) {
        std::cout << "Setting up device " << chip_id << " local: " << cluster_desc->is_chip_mmio_capable(chip_id)
                  << std::endl;

        if (cluster_desc->is_chip_mmio_capable(chip_id)) {
            int physical_device_id = cluster_desc->get_chips_with_mmio().at(chip_id);
            auto tt_device = TTDevice::create(physical_device_id, IODeviceType::PCIe);
            tt_device->init_tt_device();
            tt_devices[chip_id] = std::move(tt_device);
        } else {
            ChipId closest_mmio_chip_id = cluster_desc->get_closest_mmio_capable_chip(chip_id);
            std::unique_ptr<TTDevice>& local_tt_device = tt_devices.at(closest_mmio_chip_id);

            SocDescriptor local_soc_descriptor =
                SocDescriptor(local_tt_device->get_arch(), local_tt_device->get_chip_info());
            EthCoord target_chip = cluster_desc->get_chip_locations().at(chip_id);
            auto remote_communication = RemoteCommunication::create_remote_communication(
                local_tt_device.get(), target_chip, nullptr);  // nullptr for sysmem_manager
            remote_communication->set_remote_transfer_ethernet_cores(local_soc_descriptor.get_eth_xy_pairs_for_channels(
                cluster_desc->get_active_eth_channels(closest_mmio_chip_id)));
            std::unique_ptr<TTDevice> remote_tt_device = TTDevice::create(std::move(remote_communication));
            remote_tt_device->init_tt_device();
            tt_devices[chip_id] = std::move(remote_tt_device);
        }
    }

    return tt_devices;
}

// This test can be destructive, and should not normally run.
// Make sure to only run it on hardware which has recovery support.
// The test is disabled by default. To enable it, run with --gtest_also_run_disabled_tests.
TEST(ApiSPITTDeviceTest, DISABLED_SPIRead) {
    auto tt_devices = setup_spi_test_devices();

    for (const auto& [chip_id, tt_device] : tt_devices) {
        std::cout << "\n=== Testing SPI read on device " << chip_id << " (remote: " << tt_device->is_remote()
                  << ") ===" << std::endl;

        // Create SPI implementation for this device.
        auto spi_impl = SPITTDevice::create(tt_device.get());

        // Test SPI read functionality
        // Note: SPI addresses are chip-specific. Using a safe area for testing.
        std::vector<uint8_t> read_data(8, 0);

        // Test SPI read - should work on chips with ARC SPI support.
        spi_impl->read(SPI_BOARD_INFO_ADDR, read_data.data(), read_data.size());

        std::cout << "Read board info: ";
        for (uint8_t byte : read_data) {
            std::cout << std::dec << (int)byte << " ";
        }
        std::cout << std::endl;

        // Verify we got some data (board info shouldn't be all zeros).
        bool has_data = false;
        for (uint8_t byte : read_data) {
            if (byte != 0) {
                has_data = true;
                break;
            }
        }
        EXPECT_TRUE(has_data) << "SPI read should return non-zero board info data for device " << chip_id;
    }
}

// This test can be destructive, and should not normally run.
// Make sure to only run it on hardware which has recovery support.
// The test is disabled by default. To enable it, run with --gtest_also_run_disabled_tests.
TEST(ApiSPITTDeviceTest, DISABLED_SPIReadModifyWrite) {
    auto tt_devices = setup_spi_test_devices();

    for (const auto& [chip_id, tt_device] : tt_devices) {
        std::cout << "\n=== Testing SPI read-modify-write on device " << chip_id
                  << " (remote: " << tt_device->is_remote() << ") ===" << std::endl;

        // Create SPI implementation for this device.
        auto spi_impl = SPITTDevice::create(tt_device.get());

        // Test read-modify-write on spare/scratch area.
        // Read current value.
        std::vector<uint8_t> original_value(2, 0);
        std::cout << "spi_read from 0x" << std::hex << SPI_SPARE_AREA_ADDR << std::dec << std::endl;
        spi_impl->read(SPI_SPARE_AREA_ADDR, original_value.data(), original_value.size());

        std::cout << "Original value at 0x" << std::hex << SPI_SPARE_AREA_ADDR << ": " << std::hex << std::setfill('0')
                  << std::setw(2) << (int)original_value[1] << std::setw(2) << (int)original_value[0] << std::endl;

        // Increment value (create a change).
        std::vector<uint8_t> new_value = original_value;
        new_value[0] = new_value[0] + 1;  // wrapping_add
        if (new_value[0] == 0) {
            new_value[1] = new_value[1] + 1;
        }

        // Write back incremented value.
        std::cout << "spi_write value to spare area at 0x" << std::hex << SPI_SPARE_AREA_ADDR << std::dec << std::endl;
        spi_impl->write(SPI_SPARE_AREA_ADDR, new_value.data(), new_value.size());

        // Read back to verify.
        std::vector<uint8_t> verify_value(2, 0);
        std::cout << "spi_read from 0x" << std::hex << SPI_SPARE_AREA_ADDR << std::dec << std::endl;
        spi_impl->read(SPI_SPARE_AREA_ADDR, verify_value.data(), verify_value.size());

        std::cout << "Updated value at 0x" << std::hex << SPI_SPARE_AREA_ADDR << ": " << std::hex << std::setfill('0')
                  << std::setw(2) << (int)verify_value[1] << std::setw(2) << (int)verify_value[0] << std::endl;

        // Verify read-after-write.
        EXPECT_EQ(new_value, verify_value) << "SPI write verification failed for device " << chip_id;
    }
}

// This test can be destructive, and should not normally run.
// Make sure to only run it on hardware which has recovery support.
// The test is disabled by default. To enable it, run with --gtest_also_run_disabled_tests.
TEST(ApiSPITTDeviceTest, DISABLED_SPIUncommittedWrite) {
    auto tt_devices = setup_spi_test_devices();

    for (const auto& [chip_id, tt_device] : tt_devices) {
        std::cout << "\n=== Testing SPI uncommitted write on device " << chip_id
                  << " (remote: " << tt_device->is_remote() << ") ===" << std::endl;

        // Create SPI implementation for this device.
        auto spi_impl = SPITTDevice::create(tt_device.get());

        // Test uncommitted write on spare/scratch area.
        // Read current value first.
        std::vector<uint8_t> original_value(2, 0);
        spi_impl->read(SPI_SPARE_AREA_ADDR, original_value.data(), original_value.size());
        std::cout << "Original value at 0x" << std::hex << SPI_SPARE_AREA_ADDR << ": " << std::hex << std::setfill('0')
                  << std::setw(2) << (int)original_value[1] << std::setw(2) << (int)original_value[0] << std::endl;

        // Increment value, but don't commit it to SPI.
        // This is to verify that the values from SPI are truly fetched.
        // If this updated value is not committed to SPI, then the value read back should be the old one.
        std::vector<uint8_t> new_value = original_value;
        new_value[0] = new_value[0] + 1;  // wrapping_add
        if (new_value[0] == 0) {
            new_value[1] = new_value[1] + 1;
        }

        // Performs write to the buffer, but doesn't commit it to SPI.
        std::cout << "spi_write (uncommitted) value to spare area at 0x" << std::hex << SPI_SPARE_AREA_ADDR << std::dec
                  << std::endl;
        spi_impl->write(SPI_SPARE_AREA_ADDR, new_value.data(), new_value.size(), true);

        // Read back to verify - should match original, not new_value.
        std::vector<uint8_t> verify_value(2, 0);
        std::cout << "spi_read from 0x" << std::hex << SPI_SPARE_AREA_ADDR << std::dec << std::endl;
        spi_impl->read(SPI_SPARE_AREA_ADDR, verify_value.data(), verify_value.size());
        std::cout << "Value after uncommitted write at 0x" << std::hex << SPI_SPARE_AREA_ADDR << ": " << std::hex
                  << std::setfill('0') << std::setw(2) << (int)verify_value[1] << std::setw(2) << (int)verify_value[0]
                  << std::endl;

        EXPECT_NE(new_value, verify_value) << "SPI buffer update on read failed for device " << chip_id
                                           << " - uncommitted write should not change SPI value";
        EXPECT_EQ(original_value, verify_value)
            << "SPI read after uncommitted write should return original value for device " << chip_id;

        // Verify that the value fetched from different address was different.
        // Read wider area to check SPI handling of different sizes.
        std::vector<uint8_t> wide_value(8, 0);
        std::cout << "spi_read (wide) from 0x" << std::hex << SPI_SPARE_AREA_ADDR << std::dec << std::endl;
        spi_impl->read(SPI_SPARE_AREA_ADDR, wide_value.data(), wide_value.size());

        uint64_t wide_value_u64 = 0;
        std::memcpy(&wide_value_u64, wide_value.data(), sizeof(wide_value_u64));
        std::cout << "Wide read at 0x" << std::hex << SPI_SPARE_AREA_ADDR << ": " << std::setfill('0') << std::setw(16)
                  << wide_value_u64 << std::endl;

        // Verify first 2 bytes match our original value (not new_value).
        EXPECT_EQ(wide_value[0], verify_value[0])
            << "First byte of wide read doesn't match original value for device " << chip_id;
        EXPECT_EQ(wide_value[1], verify_value[1])
            << "Second byte of wide read doesn't match original value for device " << chip_id;
    }
}
