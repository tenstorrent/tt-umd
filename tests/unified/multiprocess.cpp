// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <thread>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.h"

using namespace tt::umd;

TEST(Multiprocess, MultiThreadSingleDevice) {
    // ?
}

TEST(Multiprocess, MultipleDrivers) {
    auto cluster1 = std::unique_ptr<Cluster>(new Cluster());
    auto cluster2 = std::unique_ptr<Cluster>(new Cluster());

    std::thread th1 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x0;
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster1->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                cluster1->write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    0,
                    core,
                    address,
                    "SMALL_READ_WRITE_TLB");
                test_utils::read_data_from_device(
                    *cluster1, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
    });

    std::thread th2 = std::thread([&] {
        auto cluster3 = std::unique_ptr<Cluster>(new Cluster());
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x0;
        for (int loop = 0; loop < 100; loop++) {
            for (const CoreCoord& core : cluster3->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
                cluster1->write_to_device(
                    vector_to_write.data(),
                    vector_to_write.size() * sizeof(std::uint32_t),
                    0,
                    core,
                    address,
                    "SMALL_READ_WRITE_TLB");
                test_utils::read_data_from_device(
                    *cluster3, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
                ASSERT_EQ(vector_to_write, readback_vec)
                    << "Vector read back from core " << core.str() << " does not match what was written";
                readback_vec = {};
            }
            address += 0x20;
        }
        cluster3->close_device();
    });

    th1.join();
    th2.join();

    cluster1->close_device();
    cluster2->close_device();
}

TEST(Multiprocess, TwoDriversSeparateProcess) {}
