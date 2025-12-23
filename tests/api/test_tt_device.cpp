// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <thread>

#include "device/api/umd/device/warm_reset.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "utils.hpp"

using namespace tt::umd;

TEST(ApiTTDeviceTest, BasicTTDeviceIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        ChipInfo chip_info = tt_device->get_chip_info();

        SocDescriptor soc_desc(tt_device->get_arch(), chip_info);

        tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        tt_device->write_to_device(data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

        tt_device->read_from_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

        ASSERT_EQ(data_write, data_read);

        data_read = std::vector<uint32_t>(data_write.size(), 0);
    }
}

TEST(ApiTTDeviceTest, TTDeviceRegIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    std::vector<uint32_t> data_write0 = {1};
    std::vector<uint32_t> data_write1 = {2};
    std::vector<uint32_t> data_read(data_write0.size(), 0);

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();
        uint64_t address = tt_device->get_architecture_implementation()->get_debug_reg_addr();

        ChipInfo chip_info = tt_device->get_chip_info();

        SocDescriptor soc_desc(tt_device->get_arch(), chip_info);

        tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        tt_device->write_to_device(data_write0.data(), tensix_core, address, data_write0.size() * sizeof(uint32_t));
        tt_device->read_from_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));
        ASSERT_EQ(data_write0, data_read);
        data_read = std::vector<uint32_t>(data_write0.size(), 0);

        tt_device->write_to_device(data_write1.data(), tensix_core, address, data_write1.size() * sizeof(uint32_t));
        tt_device->read_from_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));
        ASSERT_EQ(data_write1, data_read);
        data_read = std::vector<uint32_t>(data_write0.size(), 0);
    }
}

TEST(ApiTTDeviceTest, TTDeviceGetBoardType) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        BoardType board_type = tt_device->get_board_type();

        EXPECT_TRUE(
            board_type == BoardType::N150 || board_type == BoardType::N300 || board_type == BoardType::P100 ||
            board_type == BoardType::P150 || board_type == BoardType::P300 || board_type == BoardType::GALAXY ||
            board_type == BoardType::UBB);
    }
}

TEST(ApiTTDeviceTest, TTDeviceMultipleThreadsIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    const uint64_t address_thread0 = 0x0;
    const uint64_t address_thread1 = address_thread0 + data_write.size() * sizeof(uint32_t);
    const uint32_t num_loops = 1000;

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();
        ChipInfo chip_info = tt_device->get_chip_info();

        SocDescriptor soc_desc(tt_device->get_arch(), chip_info);

        tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        std::thread thread0([&]() {
            std::vector<uint32_t> data_read(data_write.size(), 0);
            for (uint32_t loop = 0; loop < num_loops; loop++) {
                tt_device->write_to_device(
                    data_write.data(), tensix_core, address_thread0, data_write.size() * sizeof(uint32_t));

                tt_device->read_from_device(
                    data_read.data(), tensix_core, address_thread0, data_read.size() * sizeof(uint32_t));

                ASSERT_EQ(data_write, data_read);

                data_read = std::vector<uint32_t>(data_write.size(), 0);
            }
        });

        std::thread thread1([&]() {
            std::vector<uint32_t> data_read(data_write.size(), 0);
            for (uint32_t loop = 0; loop < num_loops; loop++) {
                tt_device->write_to_device(
                    data_write.data(), tensix_core, address_thread1, data_write.size() * sizeof(uint32_t));

                tt_device->read_from_device(
                    data_read.data(), tensix_core, address_thread1, data_read.size() * sizeof(uint32_t));

                ASSERT_EQ(data_write, data_read);

                data_read = std::vector<uint32_t>(data_write.size(), 0);
            }
        });

        thread0.join();
        thread1.join();
    }
}

TEST(ApiTTDeviceTest, DISABLED_TTDeviceWarmResetAfterNocHang) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    auto arch = PCIDevice(pci_device_ids[0]).get_arch();
    if (arch == tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP()
            << "This test intentionally hangs the NOC. On Wormhole, this can cause a severe failure where even a warm "
               "reset does not recover the device, requiring a watchdog-triggered reset for recovery.";
    }

    if (is_arm_platform()) {
        // Reset isn't supported in this situation (ARM64 host), and it turns out that this doesn't just hang the NOC.
        // It hangs my whole system (Blackhole p100, ALTRAD8UD-1L2T) and requires a reboot to recover.
        GTEST_SKIP() << "Skipping test on ARM64 due to instability.";
    }

    auto cluster = std::make_unique<Cluster>();
    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    uint64_t address = 0x0;
    std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> zero_data(data.size(), 0);
    std::vector<uint8_t> readback_data(data.size(), 0);

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    SocDescriptor soc_desc(tt_device->get_arch(), tt_device->get_chip_info());

    tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    // send to core 15, 15 which will hang the NOC
    tt_device->write_to_device(data.data(), {15, 15}, address, data.size());

    // TODO: Remove this check when it is figured out why there is no hang detected on Blackhole.
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        EXPECT_THROW(tt_device->detect_hang_read(), std::runtime_error);
    }

    WarmReset::warm_reset();

    // After a warm reset, topology discovery must be performed to detect available chips.
    // Creating a Cluster triggers this discovery process, which is why a Cluster is instantiated here,
    // even though this is a TTDevice test.
    cluster = std::make_unique<Cluster>();

    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after reset.";

    // TODO: Comment this out after finding out how to detect hang reads on BH.
    // EXPECT_NO_THROW(cluster->get_chip(0)->get_tt_device()->detect_hang_read());

    tt_device.reset();

    tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    tt_device->write_to_device(zero_data.data(), tensix_core, address, zero_data.size());

    tt_device->write_to_device(data.data(), tensix_core, address, data.size());

    tt_device->read_from_device(readback_data.data(), tensix_core, address, readback_data.size());

    ASSERT_EQ(data, readback_data);
}

TEST(ApiTTDeviceTest, TestRemoteTTDevice) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    ClusterDescriptor* cluster_desc = cluster->get_cluster_description();

    auto chip_locations = cluster_desc->get_chip_locations();

    const uint32_t buf_size = 1 << 20;
    std::vector<uint8_t> zero_out_buffer(buf_size, 0);

    std::vector<uint8_t> pattern_buf(buf_size);
    for (uint32_t i = 0; i < buf_size; i++) {
        pattern_buf[i] = (uint8_t)(i % 256);
    }

    for (ChipId remote_chip_id : cluster->get_target_remote_device_ids()) {
        TTDevice* remote_tt_device = cluster->get_chip(remote_chip_id)->get_tt_device();

        std::vector<CoreCoord> tensix_cores =
            cluster->get_chip(remote_chip_id)->get_soc_descriptor().get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            remote_tt_device->write_to_device(zero_out_buffer.data(), tensix_core, 0, buf_size);

            // Setting initial value of vector explicitly to 1, to be sure it's not 0 in any case.
            std::vector<uint8_t> readback_buf(buf_size, 1);

            remote_tt_device->read_from_device(readback_buf.data(), tensix_core, 0, buf_size);

            EXPECT_EQ(zero_out_buffer, readback_buf);

            remote_tt_device->write_to_device(pattern_buf.data(), tensix_core, 0, buf_size);

            remote_tt_device->read_from_device(readback_buf.data(), tensix_core, 0, buf_size);

            EXPECT_EQ(pattern_buf, readback_buf);
        }
    }
}

TEST(ApiTTDeviceTest, MulticastIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::map<int, PciDeviceInfo> pci_devices_info = PCIDevice::enumerate_devices_info();

    const tt::ARCH arch = pci_devices_info.at(pci_device_ids[0]).get_arch();

    tt_xy_pair xy_start;
    tt_xy_pair xy_end;

    if (arch == tt::ARCH::WORMHOLE_B0) {
        xy_start = {18, 18};
        xy_end = {21, 21};
    } else if (arch == tt::ARCH::BLACKHOLE) {
        xy_start = {1, 2};
        xy_end = {4, 6};
    }

    uint64_t address = 0x0;
    std::vector<uint8_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint8_t> data_read(data_write.size(), 0);

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
        tt_device->init_tt_device();

        for (uint32_t x = xy_start.x; x <= xy_end.x; x++) {
            for (uint32_t y = xy_start.y; y <= xy_end.y; y++) {
                tt_xy_pair tensix_core = {x, y};

                std::vector<uint8_t> zeros(data_write.size(), 0);
                tt_device->write_to_device(zeros.data(), tensix_core, address, zeros.size());

                std::vector<uint8_t> readback_zeros(zeros.size(), 1);
                tt_device->read_from_device(readback_zeros.data(), tensix_core, address, readback_zeros.size());

                EXPECT_EQ(zeros, readback_zeros);
            }
        }

        tt_device->noc_multicast_write(data_write.data(), data_write.size(), xy_start, xy_end, address);

        for (uint32_t x = xy_start.x; x <= xy_end.x; x++) {
            for (uint32_t y = xy_start.y; y <= xy_end.y; y++) {
                tt_xy_pair tensix_core = {x, y};

                std::vector<uint8_t> readback(data_write.size());
                tt_device->read_from_device(readback.data(), tensix_core, address, readback.size());

                EXPECT_EQ(data_write, readback);
            }
        }
    }
}

bool verify_data(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, int device_id) {
    if (expected.size() != actual.size()) {
        std::cerr << "Device " << device_id << ": Size mismatch! Expected " << expected.size() << " but got "
                  << actual.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < expected.size(); i++) {
        if (expected[i] != actual[i]) {
            std::cerr << "Device " << device_id << ": Data mismatch at index " << i << "! Expected " << expected[i]
                      << " but got " << actual[i] << std::endl;
            return false;
        }
    }

    std::cout << "Device " << device_id << ": Data verification passed!" << std::endl;
    return true;
}

class ApiTTDeviceParamTest : public ::testing::TestWithParam<int> {};

TEST_P(ApiTTDeviceParamTest, DISABLED_SafeApiHandlesReset) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    int delay_us = GetParam();
    std::atomic<bool> sigbus_caught{false};

    TTDevice::register_sigbus_safe_handler();

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    std::map<int, std::unique_ptr<TTDevice>> tt_devices;

    tt_xy_pair tensix_core;

    for (int pci_device_id : pci_device_ids) {
        tt_devices[pci_device_id] = TTDevice::create(pci_device_id);

        tt_devices[pci_device_id]->init_tt_device();

        ChipInfo chip_info = tt_devices[pci_device_id]->get_chip_info();

        SocDescriptor soc_desc(tt_devices[pci_device_id]->get_arch(), chip_info);

        tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    }

    std::thread background_reset_thread([&]() {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
        WarmReset::warm_reset();
    });

    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(5);

    try {
        while (!sigbus_caught) {
            if (std::chrono::steady_clock::now() - start_time > timeout) {
                break;
            }

            for (int i = 0; i < 100; ++i) {
                for (int pci_device_id : pci_device_ids) {
                    tt_devices[pci_device_id]->safe_write_to_device(
                        data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

                    tt_devices[pci_device_id]->safe_read_from_device(
                        data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

                    verify_data(data_write, data_read, pci_device_id);

                    data_read = std::vector<uint32_t>(data_write.size(), 0);
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    } catch (const std::exception& e) {
        if (std::string(e.what()) == "SIGBUS") {
            sigbus_caught = true;

        } else {
            if (background_reset_thread.joinable()) {
                background_reset_thread.join();
            }
            FAIL() << "Caught unexpected exception: " << e.what();
        }
    }

    if (background_reset_thread.joinable()) {
        background_reset_thread.join();
    }

    if (sigbus_caught) {
        SUCCEED() << "Successfully triggered SIGBUS within timeout.";
    } else {
        FAIL() << "Timed out after 5 seconds without hitting SIGBUS. Reset did not invalidate mappings in time.";
    }
}

INSTANTIATE_TEST_SUITE_P(ResetTimingVariations, ApiTTDeviceParamTest, ::testing::Values(0, 10, 50, 100, 500, 1000));
