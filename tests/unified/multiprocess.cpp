// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <thread>

#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.h"

using namespace tt::umd;

// Each test tries to run a different scenario of multiprocess testing.
// Multiple threads try to go through the same TLB.
// Simulation of one device running a full workload, while others use low level TTDevice functionality.
// Multiple processes same TLB?

constexpr int NUM_PARALLEL = 10;
constexpr int NUM_LOOPS = 100;

// We want to test IO in parallel in each thread.
// But we don't want these addresses to overlap, since the data will be corrupted.
// All of this is focused on a single chip system.
void test_read_write_all_tensix_cores(Cluster* cluster, int thread_id) {
    std::cout << " Starting test_read_write_all_cores for cluster " << (uint64_t)cluster << " thread_id " << thread_id
              << std::endl;
    auto l1_size = cluster->get_soc_descriptor(0).worker_l1_size;
    auto chunk_size = l1_size / NUM_PARALLEL;

    std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::vector<uint32_t> readback_vec = {};
    std::uint32_t address = chunk_size * thread_id;

    for (int loop = 0; loop < NUM_LOOPS; loop++) {
        for (const CoreCoord& core : cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
            cluster->write_to_device(
                vector_to_write.data(),
                vector_to_write.size() * sizeof(std::uint32_t),
                0,
                core,
                address,
                "SMALL_READ_WRITE_TLB");
            test_utils::read_data_from_device(*cluster, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << core.str() << " does not match what was written";
            readback_vec = {};
        }
        address += 0x20;
    }
    std::cout << "Completed test_read_write_all_cores for cluster " << (uint64_t)cluster << " thread_id " << thread_id
              << std::endl;
}

// Single process opens multiple clusters but uses them sequentially.
TEST(Multiprocess, MultipleClusters) {
    std::vector<std::unique_ptr<Cluster>> clusters;
    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Creating cluster " << i << std::endl;
        clusters.push_back(std::unique_ptr<Cluster>(new Cluster()));
    }
    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Start device for cluster " << i << std::endl;
        clusters[i]->start_device({});
        std::cout << "Running IO for cluster " << i << std::endl;
        test_read_write_all_cores(clusters[i].get(), i);
        std::cout << "Close device for cluster " << i << std::endl;
        clusters[i]->close_device();
    }
}

// Multiple threads use single cluster for IO.
TEST(Multiprocess, MultipleThreadsSingleCluster) {
    std::unique_ptr<Cluster> cluster = std::unique_ptr<Cluster>(new Cluster());
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&] {
            std::cout << "Start device for cluster " << i << std::endl;
            cluster->start_device({});
            std::cout << "Running IO for cluster " << i << std::endl;
            test_read_write_all_cores(cluster.get(), i);
            std::cout << "Close device for cluster " << i << std::endl;
            cluster->close_device();
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Many threads open and close many clusters.
TEST(Multiprocess, MultipleThreadsMultipleClustersCreation) {
    std::vector<std::unique_ptr<Cluster>> clusters(nullptr, NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&] {
            std::cout << "Create cluster " << i << std::endl;
            clusters[i] = std::unique_ptr<Cluster>(new Cluster());
            clusters[i] = nullptr;
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Many threads start and stop many clusters.
TEST(Multiprocess, MultipleThreadsMultipleClustersRunning) {
    std::vector<std::unique_ptr<Cluster>> clusters(nullptr, NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&] {
            clusters[i] = std::unique_ptr<Cluster>(new Cluster());
            std::cout << "Start device for cluster " << i << std::endl;
            clusters[i]->start_device({});
            std::cout << "Running IO for cluster " << i << std::endl;
            test_read_write_all_cores(clusters[i].get(), i);
            std::cout << "Close device for cluster " << i << std::endl;
            clusters[i]->close_device();
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

TEST(Multiprocess, ) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    auto workload_thread = std::thread([&] {
        std::unique_ptr<Cluster> cluster = std::unique_ptr<Cluster>(new Cluster());
        std::cout << "Start device for workload cluster " << std::endl;
        cluster->start_device({});
        // for loop?
        std::cout << "Running IO for workload cluster " << std::endl;
        test_read_write_all_cores(cluster.get(), 0);
        std::cout << "Close device for workload cluster " << std::endl;
        cluster->close_device();
    });

    auto monitor_thread = std::thread([&] {
        std::cout << "Creating monitor cluster" << std::endl;
        std::unique_ptr<Cluster> cluster = std::unique_ptr<Cluster>(new Cluster());
        // for loop?
        std::cout << "Running only reads for monitor cluster, without device start " << std::endl;
        test_read_write_all_cores(cluster.get(), 0);
        std::cout << "Destroying monitor cluster" << std::endl;
    });

    auto low_level_monitor_thread = std::thread([&] {
        std::cout << "Creating low level monitor cluster" << std::endl;
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));

        std::unique_ptr<Cluster> cluster = std::unique_ptr<Cluster>(new Cluster());
        // for loop?
        std::cout << "Running only reads for low level monitor cluster, without device start " << std::endl;
        test_read_write_all_cores(cluster.get(), 0);
        std::cout << "Destroying low level monitor cluster" << std::endl;
    })
}

// TEST(Multiprocess, MultiThreadSingleDevice) {
//     // ?
// }

// TEST(Multiprocess, MultipleDrivers) {
//     std::cout << "Creating cluster 1" << std::endl;
//     auto cluster1 = std::unique_ptr<Cluster>(new Cluster());
//     std::cout << "Creating cluster 2" << std::endl;
//     auto cluster2 = std::unique_ptr<Cluster>(new Cluster());

//     std::cout << "Running IO cluster 1" << std::endl;
//     std::thread th1 = std::thread([&] {
//         // std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
//         // std::vector<uint32_t> readback_vec = {};
//         // std::uint32_t address = 0x0;
//         // for (int loop = 0; loop < 100; loop++) {
//         //     std::cout << "Running loop over cluster1: " << loop << std::endl;
//         //     for (const CoreCoord& core : cluster1->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
//         //         cluster1->write_to_device(
//         //             vector_to_write.data(),
//         //             vector_to_write.size() * sizeof(std::uint32_t),
//         //             0,
//         //             core,
//         //             address,
//         //             "SMALL_READ_WRITE_TLB");
//         //         test_utils::read_data_from_device(
//         //             *cluster1, readback_vec, 0, core, address, 40, "SMALL_READ_WRITE_TLB");
//         //         ASSERT_EQ(vector_to_write, readback_vec)
//         //             << "Vector read back from core " << core.str() << " does not match what was written";
//         //         readback_vec = {};
//         //     }
//         //     address += 0x20;
//         // }
//     });

//     std::cout << "Running creating and closing cluster 3" << std::endl;
//     std::thread th2 = std::thread([&] {
//         std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
//         std::vector<uint32_t> readback_vec = {};
//         std::uint32_t address = 0x0;
//         for (int loop = 0; loop < 100; loop++) {
//             std::cout << "Creating cluster 3 loop " << loop << std::endl;
//             auto cluster3 = std::unique_ptr<Cluster>(new Cluster());
//             std::cout << "Writing ETH core cluster 3" << std::endl;

//             auto eth_cores = cluster3->get_soc_descriptor(0).get_cores(CoreType::ETH);
//             auto eth_core = eth_cores[0];
//             cluster3->write_to_device(
//                 vector_to_write.data(),
//                 vector_to_write.size() * sizeof(std::uint32_t),
//                 0,
//                 eth_core,
//                 address,
//                 "SMALL_READ_WRITE_TLB");
//             test_utils::read_data_from_device(
//                 *cluster3, readback_vec, 0, eth_core, address, 40, "SMALL_READ_WRITE_TLB");
//             ASSERT_EQ(vector_to_write, readback_vec)
//                 << "Vector read back from eth_core " << eth_core.str() << " does not match what was written";
//             readback_vec = {};

//             address += 0x20;
//             std::cout << "Closing cluster 3" << std::endl;
//             cluster3->close_device();
//         }
//     });

//     std::cout << "Joining threads" << std::endl;
//     th1.join();
//     th2.join();

//     std::cout << "Closing cluster1 and cluster2" << std::endl;
//     cluster1->close_device();
//     cluster2->close_device();
// }

// TEST(Multiprocess, TwoDriversSeparateProcess) {}
