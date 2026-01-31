// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <memory>
#include <tt-logger/tt-logger.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <ostream>
#include <utility>
#include <vector>

#include "assert.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/jtag/jtag.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

using namespace tt;
using namespace tt::umd;

class ApiJtagDeviceTest : public ::testing::Test {
protected:
    struct DeviceData {
        std::unique_ptr<TTDevice> tt_device_;
        tt_xy_pair tensix_core_;
    };

    static void SetUpTestSuite() {
        if (!std::filesystem::exists(JtagDevice::jtag_library_path)) {
            log_warning(tt::LogUMD, "JTAG library does not exist at {}", JtagDevice::jtag_library_path.string());
            return;
        }

        auto potential_jlink_devices = Jtag(JtagDevice::jtag_library_path.c_str()).enumerate_jlink();
        if (potential_jlink_devices.empty()) {
            log_warning(tt::LogUMD, "There are no Jlink devices connected..");
            return;
        }

        auto jlink_device_count_ = JtagDevice::create()->get_device_cnt();

        if (!jlink_device_count_) {
            log_warning(tt::LogUMD, "Jlink devices discovered but not usable with current Jtag implementation.");
            return;
        }

        for (uint32_t jlink_device_id = 0; jlink_device_id < jlink_device_count_; ++jlink_device_id) {
            DeviceData device_data;
            device_data.tt_device_ = TTDevice::create(jlink_device_id, IODeviceType::JTAG);
            device_data.tt_device_->init_tt_device();
            auto soc_descriptor =
                SocDescriptor(device_data.tt_device_->get_arch(), device_data.tt_device_->get_chip_info());
            device_data.tensix_core_ = soc_descriptor.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
            device_data_.push_back(std::move(device_data));
        }

        setup_successful_ = true;
    }

    // This method runs before EACH individual test.
    void SetUp() override {
        // Check if devices were successfully set up in SetUpTestSuite.
        if (!setup_successful_) {
            GTEST_SKIP();
        }
    }

    template <typename T>
    static void check_io(const DeviceData& device, uint64_t address, const std::vector<T>& data_write) {
        std::vector<T> data_read(data_write.size(), 0);

        device.tt_device_->write_to_device(
            data_write.data(), device.tensix_core_, address, data_write.size() * sizeof(T));
        device.tt_device_->read_from_device(
            data_read.data(), device.tensix_core_, address, data_read.size() * sizeof(T));

        ASSERT_EQ(data_write, data_read);
    }

    static std::vector<DeviceData> device_data_;
    static bool setup_successful_;
};

std::vector<ApiJtagDeviceTest::DeviceData> ApiJtagDeviceTest::device_data_;
bool ApiJtagDeviceTest::setup_successful_ = false;

TEST_F(ApiJtagDeviceTest, JTagIOBasic) {
    uint64_t address = 0x0;

    std::vector<uint32_t> data_write = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    for (const auto& device : device_data_) {
        check_io(device, address, data_write);
    }
}

TEST_F(ApiJtagDeviceTest, JtagIOUnalignedAddress) {
    uint64_t address = 0x3;

    std::vector<uint32_t> data_write = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    for (const auto& device : device_data_) {
        check_io(device, address, data_write);
    }
}

TEST_F(ApiJtagDeviceTest, JtagIOLessThanWordSize) {
    uint64_t address = 0x4;

    std::vector<uint8_t> data_write = {10, 20, 30};
    for (const auto& device : device_data_) {
        check_io(device, address, data_write);
    }
}

TEST_F(ApiJtagDeviceTest, JtagIOLessThanWordSizeUnalignedAddress) {
    uint64_t address = 0x3;

    std::vector<uint8_t> data_write = {10, 20, 30};
    for (const auto& device : device_data_) {
        check_io(device, address, data_write);
    }
}

/*
 * Write to a core using PCIe and read back using JTAG.
 * Use translated coordinates to check if JTAG targets the correct core.
 */
TEST_F(ApiJtagDeviceTest, JtagTranslatedCoordsTest) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "PCI device enumeration failed. Cannot run JTAG Translated Coords Test.";
    }
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    uint64_t address = 0x0;
    bool read_occured = false;

    // Test shouldn't last long since there's a limited number of PCIe devices on the system.
    for (const auto& pci_device_id : pci_device_ids) {
        auto pci_tt_device = TTDevice::create(pci_device_id, IODeviceType::PCIe);
        if (!pci_tt_device) {
            TT_THROW("Failed to create PCI TT device.");
        }
        pci_tt_device->init_tt_device();

        ChipInfo chip_info = pci_tt_device->get_chip_info();

        tt_xy_pair tensix_core =
            SocDescriptor(pci_tt_device->get_arch(), chip_info).get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        // clear the memory first with zeros.
        pci_tt_device->write_to_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

        pci_tt_device->write_to_device(data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

        for (const auto& device : device_data_) {
            ChipInfo jtag_chip_info = device.tt_device_->get_chip_info();
            // Since we can have multiple chips with their own jlink,
            // we have to find the one which direct connection to PCIe link.
            if (jtag_chip_info.board_id == chip_info.board_id &&
                jtag_chip_info.asic_location == chip_info.asic_location) {
                device.tt_device_->read_from_device(
                    data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));
                ASSERT_EQ(data_write, data_read);
                read_occured = true;
                break;
            }
        }
        std::fill(data_read.begin(), data_read.end(), 0);
    }
    if (!read_occured) {
        GTEST_SKIP() << "No matching PCIe device found for JTAG device.";
    }
}

TEST_F(ApiJtagDeviceTest, JtagTestNoc1) {
    std::vector<uint32_t> data_write = {11, 22, 33, 44, 55, 66, 77, 88, 99, 111};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    uint64_t address = 0x0;

    for (const auto& device : device_data_) {
        SocDescriptor soc_desc(device.tt_device_->get_arch(), device.tt_device_->get_chip_info());
        tt_xy_pair test_core_noc_0 = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::NOC0)[0];
        tt_xy_pair test_core_noc_1 = soc_desc.translate_coord_to(test_core_noc_0, CoordSystem::NOC0, CoordSystem::NOC1);

        device.tt_device_->write_to_device(
            data_write.data(), test_core_noc_0, address, data_write.size() * sizeof(uint32_t));
        NocIdSwitcher noc1_switcher(NocId::NOC1);
        device.tt_device_->read_from_device(
            data_read.data(), test_core_noc_1, address, data_read.size() * sizeof(uint32_t));
        ASSERT_EQ(data_write, data_read);
        std::fill(data_read.begin(), data_read.end(), 0);
    }
}

TEST(ApiJtagClusterTest, JtagClusterIOTest) {
    if (!std::filesystem::exists(JtagDevice::jtag_library_path)) {
        GTEST_SKIP() << "JTAG library does not exist at " << JtagDevice::jtag_library_path.string();
    }

    if (!JtagDevice::create()->get_device_cnt()) {
        GTEST_SKIP() << "No usable JTAG devices with current JTAG implementation.";
    }

    std::unique_ptr<Cluster> umd_cluster =
        std::make_unique<Cluster>(ClusterOptions{.io_device_type = IODeviceType::JTAG});

    // Initialize random data.
    size_t data_size = 10;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

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
