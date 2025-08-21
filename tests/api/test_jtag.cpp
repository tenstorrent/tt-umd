// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <memory>

#include "gtest/gtest.h"
#include "tt-logger/tt-logger.hpp"
#include "umd/device/jtag/jtag.h"
#include "umd/device/jtag/jtag_device.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/tt_soc_descriptor.h"
#include "umd/device/types/communication.h"
using namespace tt::umd;

class ApiJtagDeviceTest : public ::testing::Test {
protected:
    struct DeviceData {
        std::unique_ptr<TTDevice> tt_device_;
        tt_xy_pair tensix_core_;
    };

    static void SetUpTestSuite() {
        if (!std::filesystem::exists(JtagDevice::jtag_library_path)) {
            log_warning(
                tt::LogSiliconDriver, "JTAG library does not exist at {}", JtagDevice::jtag_library_path.string());
            return;
        }

        auto potential_jlink_devices = Jtag(JtagDevice::jtag_library_path.c_str()).enumerate_jlink();
        if (!potential_jlink_devices.size()) {
            log_warning(tt::LogSiliconDriver, "There are no Jlink devices connected..");
            return;
        }

        auto jlink_device_count_ = JtagDevice::create()->get_device_cnt();

        if (!jlink_device_count_) {
            log_warning(
                tt::LogSiliconDriver, "Jlink devices discovered but not usable with current Jtag implementation.");
            return;
        }

        for (uint32_t jlink_device_id = 0; jlink_device_id < jlink_device_count_; ++jlink_device_id) {
            DeviceData device_data;
            device_data.tt_device_ = TTDevice::create(jlink_device_id, IODeviceType::JTAG);
            ChipInfo chip_info = device_data.tt_device_->get_chip_info();
            auto soc_descriptor = tt_SocDescriptor(
                device_data.tt_device_->get_arch(),
                chip_info.noc_translation_enabled,
                chip_info.harvesting_masks,
                chip_info.board_type);
            device_data.tensix_core_ = soc_descriptor.get_cores(CoreType::TENSIX, CoordSystem::NOC0)[0];
            device_data_.push_back(std::move(device_data));
        }

        setup_successful_ = true;
    }

    // This method runs before EACH individual test
    void SetUp() override {
        // Check if devices were successfully set up in SetUpTestSuite
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
