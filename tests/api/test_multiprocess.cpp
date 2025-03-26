// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <thread>

#include "l1_address_map.h"
#include "umd/device/cluster.h"

using namespace tt::umd;

TEST(MultiprocessUMD, ClusterAndTTDeviceTest) {
    const uint64_t address_thread0 = l1_mem::address_map::NCRISC_FIRMWARE_BASE;
    const uint64_t address_thread1 = address_thread0 + 0x100;
    const uint32_t num_loops = 1000;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    for (chip_id_t chip : cluster->get_target_mmio_device_ids()) {
        TTDevice* tt_device = cluster->get_tt_device(chip);

        CoreCoord tensix_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::TENSIX)[0];

        std::thread thread0([&]() {
            std::vector<uint32_t> data_write_t0 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            std::vector<uint32_t> data_read(data_write_t0.size(), 0);
            for (uint32_t loop = 0; loop < num_loops; loop++) {
                tt_device->write_to_device(
                    data_write_t0.data(), tensix_core, address_thread0, data_write_t0.size() * sizeof(uint32_t));

                tt_device->read_from_device(
                    data_read.data(), tensix_core, address_thread0, data_read.size() * sizeof(uint32_t));

                ASSERT_EQ(data_write_t0, data_read);

                data_read = std::vector<uint32_t>(data_write_t0.size(), 0);
            }
        });

        std::thread thread1([&]() {
            std::vector<uint32_t> data_write_t1 = {11, 22, 33, 44, 55, 66, 77, 88, 99, 100};
            std::vector<uint32_t> data_read(data_write_t1.size(), 0);
            for (uint32_t loop = 0; loop < num_loops; loop++) {
                cluster->write_to_device(
                    data_write_t1.data(),
                    data_write_t1.size() * sizeof(uint32_t),
                    chip,
                    tensix_core,
                    address_thread1,
                    "SMALL_READ_WRITE_TLB");

                cluster->read_from_device(
                    data_read.data(),
                    chip,
                    tensix_core,
                    address_thread1,
                    data_read.size() * sizeof(uint32_t),
                    "SMALL_READ_WRITE_TLB");

                ASSERT_EQ(data_write_t1, data_read);

                data_read = std::vector<uint32_t>(data_write_t1.size(), 0);
            }
        });

        thread0.join();
        thread1.join();
    }
}
