// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <ostream>
#include <thread>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include <sys/types.h>

using namespace tt::umd;

constexpr int NUM_PARALLEL = 4;
constexpr int NUM_LOOPS = 1000;
static constexpr int NUM_OF_BYTES_RESERVED = 128;

// Core implementation for testing IO in parallel threads.
// Partitions L1 memory between threads to avoid address overlaps.
// All of this is focused on a single chip system.
static void test_read_write_all_tensix_cores_impl(
    Cluster* cluster, int thread_id, uint32_t reserved_size = 0, bool enable_alignment = false) {
    std::cout << " Starting test_read_write_all_tensix_cores for cluster " << reinterpret_cast<uint64_t>(cluster)
              << " thread_id " << thread_id << std::endl;

    const auto l1_size = cluster->get_soc_descriptor(0).worker_l1_size;
    const auto available_size = l1_size - reserved_size;
    const auto chunk_size = available_size / NUM_PARALLEL;

    const std::vector<uint32_t> vector_to_write = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const uint32_t data_size = vector_to_write.size() * sizeof(uint32_t);
    std::vector<uint32_t> readback_vec = {};
    readback_vec.reserve(vector_to_write.size());

    uint32_t address = reserved_size + chunk_size * thread_id;
    const uint32_t start_address = address;
    const uint32_t address_next_thread = reserved_size + chunk_size * (thread_id + 1);

    for (int loop = 0; loop < NUM_LOOPS; loop++) {
        for (const CoreCoord& core : cluster->get_soc_descriptor(0).get_cores(CoreType::TENSIX)) {
            cluster->write_to_device(vector_to_write.data(), data_size, 0, core, address);
            cluster->l1_membar(0, {core});
            test_utils::read_data_from_device(*cluster, readback_vec, 0, core, address, data_size);
            ASSERT_EQ(vector_to_write, readback_vec)
                << "Vector read back from core " << core.str() << " does not match what was written";
            readback_vec.clear();
        }

        // Increment for 32 bytes, so there is an overlap of data of 8 bytes, so the thread
        // synchornization is verified.
        address += 0x20;

        // If we get into the bucket of the next thread, return to start address of this thread's bucket.
        // If we are inside other bucket can't guarantee the order of read/writes.
        if (address + data_size > address_next_thread || address + data_size > l1_size) {
            address = start_address;
        }
    }
    std::cout << "Completed test_read_write_all_tensix_cores for cluster " << reinterpret_cast<uint64_t>(cluster)
              << " thread_id " << thread_id << std::endl;
}

// We want to test IO in parallel in each thread.
// But we don't want these addresses to overlap, since the data will be corrupted.
// All of this is focused on a single chip system.
void test_read_write_all_tensix_cores(Cluster* cluster, int thread_id) {
    test_read_write_all_tensix_cores_impl(cluster, thread_id, 0, false);
}

// Same intention as test_read_write_all_tensix_cores, but without modifying first 128 bytes.
void test_read_write_all_tensix_cores_with_reserved_bytes_at_start(Cluster* cluster, int thread_id) {
    test_read_write_all_tensix_cores_impl(cluster, thread_id, NUM_OF_BYTES_RESERVED, true);
}

// Single process opens multiple clusters but uses them sequentially.
TEST(Multiprocess, MultipleClusters) {
    std::vector<std::unique_ptr<Cluster>> clusters;
    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Creating cluster " << i << std::endl;
        clusters.push_back(std::make_unique<Cluster>());
    }
    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Running IO for cluster " << i << std::endl;
        test_read_write_all_tensix_cores(clusters[i].get(), i);
        std::cout << "Finished IO for cluster " << i << std::endl;
    }
}

// Multiple threads use single cluster for IO.
TEST(Multiprocess, MultipleThreadsSingleCluster) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    std::vector<std::thread> threads;
    threads.reserve(NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&, i] {
            std::cout << "Running IO for thread " << i << " inside cluster." << std::endl;
            test_read_write_all_tensix_cores(cluster.get(), i);
            std::cout << "Finished read/write test for cluster " << i << std::endl;
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Many threads open and close many clusters.
TEST(Multiprocess, DISABLED_MultipleThreadsMultipleClustersCreation) {
    std::vector<std::thread> threads;
    threads.reserve(NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&, i] {
            std::cout << "Create cluster " << i << std::endl;
            std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
            cluster = nullptr;
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Many threads start and stop many clusters.
TEST(Multiprocess, MultipleThreadsMultipleClustersRunning) {
    std::vector<std::thread> threads;
    threads.reserve(NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&, i] {
            std::cout << "Creating cluster " << i << std::endl;
            std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
            std::cout << "Running IO for cluster " << i << std::endl;
            test_read_write_all_tensix_cores(cluster.get(), i);
            std::cout << "Finished IO for cluster " << i << std::endl;
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Many threads start and stop many clusters.
// This test runs in parallel testing the lock guarding the start/stop of the device.
TEST(Multiprocess, MultipleThreadsMultipleClustersOpenClose) {
    std::vector<std::thread> threads;
    threads.reserve(NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&, i] {
            std::unique_ptr<Cluster> cluster =
                std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
            std::cout << "Setting up risc cores and starting cluster " << i << std::endl;
            test_utils::safe_test_cluster_start(cluster.get());
            std::cout << "Running IO for cluster " << i << std::endl;
            test_read_write_all_tensix_cores_with_reserved_bytes_at_start(cluster.get(), i);
            std::cout << "Stopping cluster " << i << std::endl;
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Simulation of one device running a full workload, while others use low level TTDevice functionality.
TEST(Multiprocess, WorkloadVSMonitor) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    auto workload_thread = std::thread([&] {
        std::cout << "Creating workload cluster" << std::endl;
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
        std::cout << "Running IO for workload cluster" << std::endl;
        test_read_write_all_tensix_cores(cluster.get(), 0);
        std::cout << "Finished IO for workload cluster" << std::endl;
    });

    auto monitor_thread = std::thread([&] {
        std::cout << "Creating monitor cluster" << std::endl;
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
        std::cout << "Running only reads for monitor cluster" << std::endl;
        for (int loop = 0; loop < NUM_LOOPS; loop++) {
            uint32_t example_read;
            cluster->read_from_device(
                &example_read,
                0,
                cluster->get_soc_descriptor(0).get_cores(CoreType::ARC)[0],
                0x8003042C,
                sizeof(uint32_t));
        }
        std::cout << "Destroying monitor cluster" << std::endl;
    });

    auto low_level_monitor_thread = std::thread([&] {
        std::cout << "Creating low level monitor cluster" << std::endl;
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
        tt_device->init_tt_device();

        SocDescriptor soc_desc = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
        CoreCoord arc_core = soc_desc.get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];

        std::cout << "Running only reads for low level monitor cluster, without device start " << std::endl;
        for (int loop = 0; loop < NUM_LOOPS; loop++) {
            uint32_t example_read;
            tt_device->read_from_device(&example_read, arc_core, 0x8003042C, sizeof(uint32_t));
        }
        std::cout << "Destroying low level monitor cluster" << std::endl;
    });

    workload_thread.join();
    monitor_thread.join();
    low_level_monitor_thread.join();
}

TEST(Multiprocess, LongLivedMonitor) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    auto low_level_monitor_thread = std::thread([&] {
        std::cout << "Creating low level monitor cluster" << std::endl;
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
        tt_device->init_tt_device();

        SocDescriptor soc_desc = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
        CoreCoord arc_core = soc_desc.get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];

        std::cout << "Running only reads for low level monitor cluster, without device start " << std::endl;
        for (int loop = 0; loop < NUM_LOOPS; loop++) {
            uint32_t example_read;
            tt_device->read_from_device(&example_read, arc_core, 0x8003042C, sizeof(uint32_t));
        }
        std::cout << "Destroying low level monitor cluster" << std::endl;
    });

    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Creating cluster " << i << std::endl;
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
        std::cout << "Running IO for cluster " << i << std::endl;
        test_read_write_all_tensix_cores(cluster.get(), i);
        std::cout << "Finished IO for cluster " << i << std::endl;
    }

    low_level_monitor_thread.join();
}

TEST(Multiprocess, ClusterAndTTDeviceTest) {
    const uint64_t address_thread0 = 0x1000;
    const uint64_t address_thread1 = address_thread0 + 0x100;
    const uint32_t num_loops = 1000;

    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    for (ChipId chip : cluster->get_target_mmio_device_ids()) {
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
                    data_write_t1.data(), data_write_t1.size() * sizeof(uint32_t), chip, tensix_core, address_thread1);
                cluster->l1_membar(chip, {tensix_core});

                cluster->read_from_device(
                    data_read.data(), chip, tensix_core, address_thread1, data_read.size() * sizeof(uint32_t));

                ASSERT_EQ(data_write_t1, data_read);

                data_read = std::vector<uint32_t>(data_write_t1.size(), 0);
            }
        });

        thread0.join();
        thread1.join();
    }
}

// Test to demonstrate race condition in DMA operations when multiple processes
// use TTDevice objects with the same underlying PCIDevice
TEST(Multiprocess, DMAWriteReadRaceCondition) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    // Use the first available PCI device for this test.
    const int test_device_id = pci_device_ids.at(0);
    const int num_processes = 4;
    const int num_iterations = 500;
    const uint64_t test_address = 0x1000;
    const size_t data_size = 1024;  // 1KB data per operation

    std::cout << "Testing DMA race condition on PCI device " << test_device_id << std::endl;

    std::vector<std::thread> process_threads;
    process_threads.reserve(num_processes);

    for (int process_id = 0; process_id < num_processes; process_id++) {
        process_threads.push_back(std::thread([=]() {
            std::cout << "Process " << process_id << ": Creating TTDevice for PCI device " << test_device_id
                      << std::endl;

            // Each process creates its own TTDevice object with the same PCIDevice.
            std::unique_ptr<TTDevice> tt_device = TTDevice::create(test_device_id);
            tt_device->init_tt_device();

            SocDescriptor soc_desc = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
            CoreCoord tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

            // Create unique data pattern for this process.
            std::vector<uint32_t> write_data(data_size / sizeof(uint32_t));
            std::vector<uint32_t> read_data(data_size / sizeof(uint32_t));

            for (size_t i = 0; i < write_data.size(); i++) {
                write_data[i] = (process_id << 24) | (i & 0xFFFFFF);  // Unique pattern per process
            }

            std::cout << "Process " << process_id << ": Starting DMA operations" << std::endl;

            for (int iter = 0; iter < num_iterations; iter++) {
                try {
                    // Use different addresses per process to avoid data corruption.
                    uint64_t process_address = test_address + (process_id * data_size * 2);

                    // Write data using DMA.
                    tt_device->dma_write_to_device(write_data.data(), data_size, tensix_core, process_address);

                    // Read data back using DMA.
                    std::fill(read_data.begin(), read_data.end(), 0);
                    tt_device->dma_read_from_device(read_data.data(), data_size, tensix_core, process_address);

                    // Verify data integrity.
                    ASSERT_EQ(write_data, read_data)
                        << "Data mismatch in process " << process_id << " iteration " << iter;

                } catch (const std::exception& e) {
                    std::cout << "Process " << process_id << " iteration " << iter << " failed: " << e.what()
                              << std::endl;
                    FAIL() << "DMA operation failed in process " << process_id;
                }
            }

            std::cout << "Process " << process_id << ": Completed " << num_iterations << " DMA operations successfully"
                      << std::endl;
        }));
    }

    // Wait for all process threads to complete.
    for (auto& thread : process_threads) {
        thread.join();
    }

    std::cout << "DMA race condition test completed" << std::endl;
}

TEST(Multiprocess, DMAWriteReadRaceConditionProcessIsolation) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    constexpr int NUM_PROCESSES = 4;
    std::vector<pid_t> pids;

    // Use the first available PCI device for this test.
    const int test_device_id = pci_device_ids.at(0);
    constexpr int num_iterations = 500;
    constexpr uint64_t test_address = 0x1000;
    constexpr size_t data_size = 1024;  // 1KB data per operation.

    std::cout << "Testing DMA race condition (real fork) on PCI device " << test_device_id << std::endl;

    for (int process_id = 0; process_id < NUM_PROCESSES; process_id++) {
        pid_t pid = fork();
        if (pid == 0) {  // Child Process.
            std::cout << "Process " << process_id << " with pid " << pid << ": Starting DMA operations" << std::endl;

            // Each process creates its own TTDevice object with the same PCIDevice.
            std::unique_ptr<TTDevice> tt_device = TTDevice::create(test_device_id);
            tt_device->init_tt_device();

            SocDescriptor soc_desc = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
            CoreCoord tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

            // Create unique data pattern for this process.
            std::vector<uint32_t> write_data(data_size / sizeof(uint32_t));
            std::vector<uint32_t> read_data(data_size / sizeof(uint32_t));

            for (size_t i = 0; i < write_data.size(); i++) {
                write_data[i] = (process_id << 24) | (i & 0xFFFFFF);  // Unique pattern per process.
            }

            for (int iter = 0; iter < num_iterations; iter++) {
                try {
                    // Use different addresses per process to avoid data corruption.
                    uint64_t process_address = test_address + (process_id * data_size * 2);

                    // Write data using DMA.
                    tt_device->dma_write_to_device(write_data.data(), data_size, tensix_core, process_address);

                    // Read data back using DMA.
                    std::fill(read_data.begin(), read_data.end(), 0);
                    tt_device->dma_read_from_device(read_data.data(), data_size, tensix_core, process_address);

                    // Verify data integrity.
                    if (write_data != read_data) {
                        std::cout << "Data mismatch in process " << process_id << " iteration " << iter << std::endl;
                        _exit(1);  // Return 1 for Data Mismatch.
                    }

                } catch (const std::exception& e) {
                    std::cout << "Process " << process_id << " iteration " << iter << " failed: " << e.what()
                              << std::endl;
                    _exit(2);  // Return 2 for Exception.
                }
            }

            std::cout << "Process " << process_id << ": Completed " << num_iterations << " DMA operations successfully"
                      << std::endl;
            _exit(0);  // Return 0 for Success.
        }
        // Parent process.
        pids.push_back(pid);
    }

    // Wait for all process threads to complete.
    for (pid_t p : pids) {
        int status;
        waitpid(p, &status, 0);
        if (WIFEXITED(status)) {
            EXPECT_EQ(WEXITSTATUS(status), 0)
                << "Child process " << p << " failed with exit code " << WEXITSTATUS(status);
        } else {
            ADD_FAILURE() << "Child process " << p << " exited abnormally";
        }
    }

    std::cout << "DMA race condition test (real fork) completed" << std::endl;
}
