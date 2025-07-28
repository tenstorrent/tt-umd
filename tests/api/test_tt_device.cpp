// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <thread>

#include "device/api/umd/device/warm_reset.h"
#include "gtest/gtest.h"
#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_device/remote_wormhole_tt_device.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/wormhole_implementation.h"

using namespace tt::umd;

TEST(ApiTTDeviceTest, BasicTTDeviceIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);

    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);

        ChipInfo chip_info = tt_device->get_chip_info();

        tt_SocDescriptor soc_desc(
            tt_device->get_arch(), chip_info.noc_translation_enabled, chip_info.harvesting_masks, chip_info.board_type);

        tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        tt_device->write_to_device(data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

        tt_device->read_from_device(data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

        ASSERT_EQ(data_write, data_read);

        data_read = std::vector<uint32_t>(data_write.size(), 0);
    }
}

TEST(ApiTTDeviceTest, TTDeviceGetBoardType) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    for (int pci_device_id : pci_device_ids) {
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);

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

        ChipInfo chip_info = tt_device->get_chip_info();

        tt_SocDescriptor soc_desc(
            tt_device->get_arch(), chip_info.noc_translation_enabled, chip_info.harvesting_masks, chip_info.board_type);

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

TEST(ApiTTDeviceTest, TTDeviceWarmResetAfterNocHang) {
    std::cout << "before std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();" << std::endl;
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    std::cout << "before uint64_t address = 0x0;" << std::endl;
    uint64_t address = 0x0;
    std::cout << "before std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};" << std::endl;
    std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    std::cout << "before std::vector<uint8_t> zero_data(data.size(), 0);" << std::endl;
    std::vector<uint8_t> zero_data(data.size(), 0);
    std::cout << "before std::vector<uint8_t> readback_data(data.size(), 0);" << std::endl;
    std::vector<uint8_t> readback_data(data.size(), 0);

    std::cout << "before for (int pci_device_id : pci_device_ids) {" << std::endl;
    for (int pci_device_id : pci_device_ids) {
        std::cout << "before std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);" << std::endl;
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);

        std::cout << "before ChipInfo chip_info = tt_device->get_chip_info();" << std::endl;
        ChipInfo chip_info = tt_device->get_chip_info();

        std::cout << "before tt_SocDescriptor soc_desc(...);" << std::endl;
        tt_SocDescriptor soc_desc(
            tt_device->get_arch(), chip_info.noc_translation_enabled, chip_info.harvesting_masks, chip_info.board_type);

        std::cout << "before tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];"
                  << std::endl;
        tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

        std::cout << "before tt_device->write_to_device(data.data(), {15, 15}, address, data.size());" << std::endl;
        // send to core 15, 15 which will hang the NOC
        tt_device->write_to_device(data.data(), {15, 15}, address, data.size());

        std::cout << "before if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {" << std::endl;
        // TODO: Remove this check when it is figured out why there is no hang detected on Blackhole.
        if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
            std::cout << "before EXPECT_THROW(tt_device->detect_hang_read(), std::runtime_error);" << std::endl;
            EXPECT_THROW(tt_device->detect_hang_read(), std::runtime_error);
        }

        std::cout << "before WarmReset::warm_reset();" << std::endl;
        WarmReset::warm_reset();

        std::cout << "before EXPECT_NO_THROW(tt_device->detect_hang_read());" << std::endl;
        EXPECT_NO_THROW(tt_device->detect_hang_read());

        std::cout << "before auto cluster = std::make_unique<Cluster>();" << std::endl;
        // Make cluster so that topology discovery does chip detection
        auto cluster = std::make_unique<Cluster>();

        std::cout
            << "before EXPECT_FALSE(cluster->get_target_device_ids().empty()) << \"No chips present after reset.\";"
            << std::endl;
        EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after reset.";

        std::cout << "before tt_device.reset();" << std::endl;
        tt_device.reset();

        std::cout << "before tt_device = TTDevice::create(pci_device_id);" << std::endl;
        tt_device = TTDevice::create(pci_device_id);

        std::cout << "before tt_device->write_to_device(zero_data.data(), tensix_core, address, zero_data.size());"
                  << std::endl;
        tt_device->write_to_device(zero_data.data(), tensix_core, address, zero_data.size());

        std::cout << "before tt_device->write_to_device(data.data(), tensix_core, address, data.size());" << std::endl;
        tt_device->write_to_device(data.data(), tensix_core, address, data.size());

        std::cout
            << "before tt_device->read_from_device(readback_data.data(), tensix_core, address, readback_data.size());"
            << std::endl;
        tt_device->read_from_device(readback_data.data(), tensix_core, address, readback_data.size());

        std::cout << "before ASSERT_EQ(data, readback_data);" << std::endl;
        ASSERT_EQ(data, readback_data);
    }
}

TEST(ApiTTDeviceTest, TestRemoteTTDevice) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    tt_ClusterDescriptor* cluster_desc = cluster->get_cluster_description();

    auto chip_locations = cluster_desc->get_chip_locations();

    const uint32_t buf_size = 1 << 20;
    std::vector<uint8_t> zero_out_buffer(buf_size, 0);

    std::vector<uint8_t> pattern_buf(buf_size);
    for (uint32_t i = 0; i < buf_size; i++) {
        pattern_buf[i] = (uint8_t)(i % 256);
    }

    for (chip_id_t remote_chip_id : cluster->get_target_remote_device_ids()) {
        eth_coord_t remote_eth_coord = chip_locations.at(remote_chip_id);

        LocalChip* closest_local_chip =
            cluster->get_local_chip(cluster_desc->get_closest_mmio_capable_chip(remote_chip_id));

        std::unique_ptr<RemoteWormholeTTDevice> remote_tt_device =
            std::make_unique<RemoteWormholeTTDevice>(closest_local_chip, remote_eth_coord);

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
