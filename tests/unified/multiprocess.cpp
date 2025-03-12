// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <thread>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.h"

using namespace tt::umd;

// Each test tries to run a different scenario of multiprocess testing.
// Single process opens multiple clusters but uses them sequentially.
// Multiple threads use single cluster for IO.
// Many threads open and close many clusters.
// Many threads start and stop many clusters.
// Multiple threads try to go through the same TLB.
// Simulation of one device running a full workload, while others use low level TTDevice functionality.
// Multiple processes same TLB?

TEST(Multiprocess, MultiThreadSingleDevice) {
    // ?
}

TEST(Multiprocess, MultipleDrivers) {
    std::cout << "Creating cluster 1" << std::endl;
    auto cluster1 = std::unique_ptr<Cluster>(new Cluster());
    std::cout << "Creating cluster 2" << std::endl;
    auto cluster2 = std::unique_ptr<Cluster>(new Cluster());

    std::cout << "Running IO cluster 1" << std::endl;
    std::thread th1 = std::thread([&] {
        // std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        // std::vector<uint32_t> readback_vec = {};
        // std::uint32_t address = 0x0;
        // for (int loop = 0; loop < 100; loop++) {
        //     std::cout << "Running loop over cluster1: " << loop << std::endl;
        //     for (const CoreCoord& core : cluster1->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
        //         cluster1->write_to_device(
        //             vector_to_write.data(),
        //             vector_to_write.size() * sizeof(std::uint32_t),
        //             0,
        //             core,
        //             address,
        //             "SMALL_READ_WRITE_TLB");
        //         test_utils::read_data_from_device(
        //             *cluster1, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
        //         ASSERT_EQ(vector_to_write, readback_vec)
        //             << "Vector read back from core " << core.str() << " does not match what was written";
        //         readback_vec = {};
        //     }
        //     address += 0x20;
        // }
    });

    std::cout << "Running creating and closing cluster 3" << std::endl;
    std::thread th2 = std::thread([&] {
        std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        std::vector<uint32_t> readback_vec = {};
        std::uint32_t address = 0x0;
        for (int loop = 0; loop < 100; loop++) {
            std::cout << "Creating cluster 3 loop " << loop << std::endl;
            auto cluster3 = std::unique_ptr<Cluster>(new Cluster());
            std::cout << "Writing ETH core cluster 3" << std::endl;

            auto eth_cores = cluster3->get_soc_descriptor(0).get_cores(CoreType::ETH);
            auto eth_core = eth_cores[0];
            cluster3->write_to_device(
                vector_to_write.data(),
                vector_to_write.size() * sizeof(std::uint32_t),
                0,
                eth_core,
                address,
                "SMALL_READ_WRITE_TLB");
            test_utils::read_data_from_device(
                *cluster3, readback_vec, 0, eth_core, address, 40, "SMALL_READ_WRITE_TLB");
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from eth_core " << eth_core.str() << " does not match what was written";
            readback_vec = {};

            address += 0x20;
            std::cout << "Closing cluster 3" << std::endl;
            cluster3->close_device();
        }
    });

    std::cout << "Joining threads" << std::endl;
    th1.join();
    th2.join();

    std::cout << "Closing cluster1 and cluster2" << std::endl;
    cluster1->close_device();
    cluster2->close_device();
}

TEST(Multiprocess, TwoDriversSeparateProcess) {}
