// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/exceptions.hpp"
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
            board_type == BoardType::P150 || board_type == BoardType::P300 || board_type == BoardType::UBB);
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

TEST(ApiTTDeviceTest, TestRemoteTTDevice) {
    // The test does large transfers to remote chip, so system memory significantly speeds up the tests.
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = get_num_host_ch_for_test()});

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
