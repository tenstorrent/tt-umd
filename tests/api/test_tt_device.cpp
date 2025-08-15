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
        tt_device->init_tt_device();
        tt_device->wait_arc_core_start();

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
        tt_device->init_tt_device();
        tt_device->wait_arc_core_start();

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
        tt_device->wait_arc_core_start();
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
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    uint64_t address = 0x0;
    std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> zero_data(data.size(), 0);
    std::vector<uint8_t> readback_data(data.size(), 0);

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();
    tt_device->wait_arc_core_start();

    ChipInfo chip_info = tt_device->get_chip_info();

    tt_SocDescriptor soc_desc(
        tt_device->get_arch(), chip_info.noc_translation_enabled, chip_info.harvesting_masks, chip_info.board_type);

    tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    // send to core 15, 15 which will hang the NOC
    tt_device->write_to_device(data.data(), {15, 15}, address, data.size());

    // TODO: Remove this check when it is figured out why there is no hang detected on Blackhole.
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        EXPECT_THROW(tt_device->detect_hang_read(), std::runtime_error);
    }

    WarmReset::warm_reset();

    // Make cluster so that topology discovery does chip detection
    auto cluster = std::make_unique<Cluster>();

    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after reset.";

    EXPECT_NO_THROW(cluster->get_chip(0)->get_tt_device()->detect_hang_read());

    tt_device.reset();

    tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();
    tt_device->wait_arc_core_start();

    tt_device->write_to_device(zero_data.data(), tensix_core, address, zero_data.size());

    tt_device->write_to_device(data.data(), tensix_core, address, data.size());

    tt_device->read_from_device(readback_data.data(), tensix_core, address, readback_data.size());

    ASSERT_EQ(data, readback_data);
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

        chip_id_t gateway_id = cluster_desc->get_closest_mmio_capable_chip(remote_chip_id);
        LocalChip* closest_local_chip = cluster->get_local_chip(gateway_id);
        std::unique_ptr<RemoteCommunication> remote_communication =
            std::make_unique<RemoteCommunication>(closest_local_chip, closest_local_chip->get_sysmem_manager());
        remote_communication->set_remote_transfer_ethernet_cores(cluster_desc->get_active_eth_channels(gateway_id));
        std::unique_ptr<RemoteWormholeTTDevice> remote_tt_device = std::make_unique<RemoteWormholeTTDevice>(
            closest_local_chip, std::move(remote_communication), remote_eth_coord);
        remote_tt_device->init_tt_device();

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
