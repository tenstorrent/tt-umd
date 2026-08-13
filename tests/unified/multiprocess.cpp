// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/setup_risc_cores.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt::umd;

/**
 * Helper that reads data from a device core using the appropriate mechanism for the
 * current architecture. On Wormhole B0, PCIe DMA reads are required/preferred, so
 * dma_read_from_device is used. On other architectures (including Blackhole), the standard
 * read_from_device path is used instead.
 */
void read_data_based_on_architecture(
    TTDevice& tt_device, CoreCoord core, void* mem_ptr, uint64_t address, size_t size) {
    if (tt_device.get_arch() == tt::ARCH::WORMHOLE_B0) {
        tt_device.dma_read_from_device(mem_ptr, size, core, address);
    } else {
        tt_device.read_from_device(mem_ptr, core, address, size);
    }
}

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
    // NOTE: On Blackhole CMFW >19.3, TENSIX cores reserve address 0x10 for ARC writing throttle state
    // that is consumed by kernels. We need to skip ahead of this address to prevent failing these checks.
    test_read_write_all_tensix_cores_impl(cluster, thread_id, NUM_OF_BYTES_RESERVED, true);
}

// Single process opens multiple clusters but uses them sequentially.
TEST(Multiprocess, MultipleClusters) {
    std::vector<std::unique_ptr<Cluster>> clusters;
    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Creating cluster " << i << std::endl;
        clusters.push_back(test_utils::make_default_test_cluster());
    }
    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Running IO for cluster " << i << std::endl;
        test_read_write_all_tensix_cores_with_reserved_bytes_at_start(clusters[i].get(), i);
        std::cout << "Finished IO for cluster " << i << std::endl;
    }
}

// Multiple threads use single cluster for IO.
TEST(Multiprocess, MultipleThreadsSingleCluster) {
    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
    std::vector<std::thread> threads;
    threads.reserve(NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&, i] {
            std::cout << "Running IO for thread " << i << " inside cluster." << std::endl;
            test_read_write_all_tensix_cores_with_reserved_bytes_at_start(cluster.get(), i);
            std::cout << "Finished read/write test for cluster " << i << std::endl;
        }));
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Many threads open and close many clusters.
TEST(Multiprocess, MultipleThreadsMultipleClustersCreation) {
    std::vector<std::thread> threads;
    threads.reserve(NUM_PARALLEL);
    for (int i = 0; i < NUM_PARALLEL; i++) {
        threads.push_back(std::thread([&, i] {
            std::cout << "Create cluster " << i << std::endl;
            std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
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
            std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
            std::cout << "Running IO for cluster " << i << std::endl;
            test_read_write_all_tensix_cores_with_reserved_bytes_at_start(cluster.get(), i);
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
                test_utils::make_default_test_cluster(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
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

    auto workload_thread = std::thread([&] {
        std::cout << "Creating workload cluster" << std::endl;
        std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
        std::cout << "Running IO for workload cluster" << std::endl;
        test_read_write_all_tensix_cores_with_reserved_bytes_at_start(cluster.get(), 0);
        std::cout << "Finished IO for workload cluster" << std::endl;
    });

    auto monitor_thread = std::thread([&] {
        std::cout << "Creating monitor cluster" << std::endl;
        std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
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
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->init_tt_device();

        const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
        CoreCoord arc_core = soc_desc.get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];

        std::cout << "Running only reads for low level monitor cluster, without device start " << std::endl;
        for (int loop = 0; loop < NUM_LOOPS; loop++) {
            uint32_t example_read;
            tt_device->read_from_device(&example_read, arc_core, 0x8003042C, sizeof(uint32_t));
        }
        std::cout << "Destroying low level monitor cluster" << std::endl;
        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    });

    workload_thread.join();
    monitor_thread.join();
    low_level_monitor_thread.join();
}

TEST(Multiprocess, LongLivedMonitor) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    auto low_level_monitor_thread = std::thread([&] {
        std::cout << "Creating low level monitor cluster" << std::endl;
        std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
        tt_device->set_power_state(TTDevice::PowerState::BUSY);
        tt_device->init_tt_device();

        const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
        CoreCoord arc_core = soc_desc.get_cores(CoreType::ARC, CoordSystem::TRANSLATED)[0];

        std::cout << "Running only reads for low level monitor cluster, without device start " << std::endl;
        for (int loop = 0; loop < NUM_LOOPS; loop++) {
            uint32_t example_read;
            tt_device->read_from_device(&example_read, arc_core, 0x8003042C, sizeof(uint32_t));
        }
        std::cout << "Destroying low level monitor cluster" << std::endl;
        tt_device->set_power_state(TTDevice::PowerState::IDLE);
    });

    for (int i = 0; i < NUM_PARALLEL; i++) {
        std::cout << "Creating cluster " << i << std::endl;
        std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();
        std::cout << "Running IO for cluster " << i << std::endl;
        test_read_write_all_tensix_cores_with_reserved_bytes_at_start(cluster.get(), i);
        std::cout << "Finished IO for cluster " << i << std::endl;
    }

    low_level_monitor_thread.join();
}

TEST(Multiprocess, ClusterAndTTDeviceTest) {
    const uint64_t address_thread0 = 0x1000;
    const uint64_t address_thread1 = address_thread0 + 0x100;
    const uint32_t num_loops = 1000;

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

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
            tt_device->set_power_state(TTDevice::PowerState::BUSY);
            tt_device->init_tt_device();

            const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
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

                    // Read data back using architecture-specific method.
                    std::fill(read_data.begin(), read_data.end(), 0);
                    read_data_based_on_architecture(
                        *tt_device, tensix_core, read_data.data(), process_address, data_size);

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
            tt_device->set_power_state(TTDevice::PowerState::IDLE);
        }));
    }

    // Wait for all process threads to complete.
    for (auto& thread : process_threads) {
        thread.join();
    }

    std::cout << "DMA race condition test completed" << std::endl;
}

TEST(Multiprocess, DISABLED_DMAWriteReadRaceConditionProcessIsolation) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

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
            tt_device->set_power_state(TTDevice::PowerState::BUSY);
            tt_device->init_tt_device();

            const SocDescriptor& soc_desc = tt_device->get_soc_descriptor();
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

                    // Read data back using architecture-specific method.
                    std::fill(read_data.begin(), read_data.end(), 0);
                    read_data_based_on_architecture(
                        *tt_device, tensix_core, read_data.data(), process_address, data_size);

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

namespace dma_reads_mixed_repro {

// Tag layout mirrors the tt-metal/ttexalens repro this test is modeled on: worker(4b) |
// noc_x(6b) | noc_y(6b) | iteration(16b). Every word of the payload carries the same tag, so a
// short readback still identifies who actually produced the data.
constexpr int NUM_WORKERS = 16;
constexpr int NUM_ITERATIONS = 300;
constexpr uint64_t SCRATCH_ADDR = 0x10000;
constexpr size_t NUM_BYTES = 256;
constexpr size_t NUM_WORDS = NUM_BYTES / sizeof(uint32_t);
constexpr int MAX_SAMPLES = 8;

uint32_t encode_tag(uint32_t worker_id, uint32_t noc_x, uint32_t noc_y, uint32_t iteration) {
    return ((worker_id & 0xF) << 28) | ((noc_x & 0x3F) << 22) | ((noc_y & 0x3F) << 16) | (iteration & 0xFFFF);
}

struct DecodedTag {
    uint32_t worker_id;
    uint32_t noc_x;
    uint32_t noc_y;
    uint32_t iteration;
};

DecodedTag decode_tag(uint32_t word) {
    return DecodedTag{(word >> 28) & 0xF, (word >> 22) & 0x3F, (word >> 16) & 0x3F, word & 0xFFFF};
}

struct ForeignSample {
    int iteration;
    DecodedTag got;
};

// Plain-old-data result block. One slot per worker, living in a MAP_SHARED|MAP_ANONYMOUS
// mapping created by the parent before fork(), so each child can report back without a
// pipe/queue: the parent reads every slot after waitpid() reaps the writer.
struct WorkerResult {
    CoreCoord core;
    int completed_iterations = 0;
    int stale = 0;
    int foreign = 0;
    int num_samples = 0;
    ForeignSample samples[MAX_SAMPLES] = {};
    bool errored = false;
    char error_message[256] = {};
};

// Core of one worker: pin to a NOC core, then repeatedly tear down and re-create the TTDevice
// (the mechanism under test, standing in for ttexalens' per-iteration init_ttexalens() in the
// original repro), write a tagged payload with a plain MMIO write ("noc_write" in the repro),
// and read it back via dma_read_from_device. A "foreign" result means the DMA read landed on
// data tagged for a different worker/core -- the completed transfer was matched to the wrong
// requester.
void run_worker(int worker_id, int pci_device_id, WorkerResult* result, pthread_barrier_t* start_barrier) {
    try {
        CoreCoord core;
        {
            std::unique_ptr<TTDevice> probe_device = TTDevice::create(pci_device_id);
            probe_device->init_tt_device();
            std::vector<CoreCoord> cores =
                probe_device->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED);
            core = cores.at(worker_id % cores.size());
        }
        result->core = core;

        pthread_barrier_wait(start_barrier);

        std::vector<uint32_t> payload(NUM_WORDS);
        std::vector<uint32_t> readback(NUM_WORDS);

        for (int iteration = 0; iteration < NUM_ITERATIONS; iteration++) {
            std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_id);
            tt_device->init_tt_device();

            std::fill(
                payload.begin(),
                payload.end(),
                encode_tag(static_cast<uint32_t>(worker_id), core.x, core.y, static_cast<uint32_t>(iteration)));
            std::fill(readback.begin(), readback.end(), 0);

            tt_device->write_to_device(payload.data(), core, SCRATCH_ADDR, NUM_BYTES);
            tt_device->dma_read_from_device(readback.data(), NUM_BYTES, core, SCRATCH_ADDR);

            result->completed_iterations++;
            if (readback == payload) {
                continue;
            }

            DecodedTag got = decode_tag(readback[0]);
            if (got.worker_id == static_cast<uint32_t>(worker_id) && got.noc_x == core.x && got.noc_y == core.y) {
                result->stale++;
            } else {
                result->foreign++;
                if (result->num_samples < MAX_SAMPLES) {
                    result->samples[result->num_samples++] = ForeignSample{iteration, got};
                }
            }
        }
    } catch (const std::exception& e) {
        result->errored = true;
        std::strncpy(result->error_message, e.what(), sizeof(result->error_message) - 1);
    }
}

}  // namespace dma_reads_mixed_repro

// Reproduces a reported bug: a PCIe DMA read targeting one NOC core occasionally returns the
// payload written to a different NOC core once the device/link is repeatedly re-initialized
// while several processes drive concurrent DMA traffic against distinct cores on the same chip.
// Modeled directly on the tt-metal/ttexalens repro (same worker|noc_x|noc_y|iteration tag, same
// stale-vs-foreign classification) but reinitializes via UMD's own TTDevice::create() /
// init_tt_device() instead of ttexalens, and drives I/O the same way UMD itself would: a plain
// write_to_device ("noc_write") followed by a dma_read_from_device readback.
//
// Disabled by default, like DISABLED_DMAWriteReadRaceConditionProcessIsolation above (real
// fork() alongside gtest has known flakiness, see issue #2579) -- run explicitly against real
// Wormhole hardware with --gtest_also_run_disabled_tests to check for the bug.
TEST(Multiprocess, DISABLED_DmaReadMixedCoreRepro) {
    using namespace dma_reads_mixed_repro;

    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();
    ASSERT_FALSE(pci_device_ids.empty());
    const int pci_device_id = pci_device_ids.at(0);

    void* barrier_mem =
        mmap(nullptr, sizeof(pthread_barrier_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(barrier_mem, MAP_FAILED);
    pthread_barrier_t* start_barrier = static_cast<pthread_barrier_t*>(barrier_mem);
    pthread_barrierattr_t barrier_attr;
    pthread_barrierattr_init(&barrier_attr);
    pthread_barrierattr_setpshared(&barrier_attr, PTHREAD_PROCESS_SHARED);
    ASSERT_EQ(pthread_barrier_init(start_barrier, &barrier_attr, NUM_WORKERS), 0);

    void* results_mem =
        mmap(nullptr, sizeof(WorkerResult) * NUM_WORKERS, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(results_mem, MAP_FAILED);
    WorkerResult* results = static_cast<WorkerResult*>(results_mem);

    std::cout << "Testing DMA read mixed-core repro on PCI device " << pci_device_id << " with " << NUM_WORKERS
              << " workers, " << NUM_ITERATIONS << " iterations each (cores shown as NOC translated x,y)" << std::endl;

    std::vector<pid_t> pids;
    pids.reserve(NUM_WORKERS);
    for (int worker_id = 0; worker_id < NUM_WORKERS; worker_id++) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "fork() failed for worker " << worker_id;
        if (pid == 0) {
            run_worker(worker_id, pci_device_id, &results[worker_id], start_barrier);
            _exit(0);
        }
        pids.push_back(pid);
    }

    for (pid_t pid : pids) {
        int status = 0;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "worker process " << pid << " exited abnormally";
    }

    int total_stale = 0;
    int total_foreign = 0;
    int total_iterations = 0;
    auto core_str = [](int x, int y) { return "(" + std::to_string(x) + "," + std::to_string(y) + ")"; };

    std::cout << std::right << std::setw(3) << "wk" << std::left << std::setw(9) << "  core" << std::right
              << std::setw(6) << "inits" << std::setw(7) << "stale" << std::setw(8) << "foreign"
              << "  status" << std::endl;
    for (int worker_id = 0; worker_id < NUM_WORKERS; worker_id++) {
        const WorkerResult& r = results[worker_id];
        ASSERT_FALSE(r.errored) << "worker " << worker_id << " failed: " << r.error_message;

        const char* tag = r.foreign > 0 ? "FOREIGN" : (r.stale > 0 ? "stale-only" : "clean");
        std::cout << std::right << std::setw(3) << worker_id << "  " << std::left << std::setw(9)
                  << core_str(r.core.x, r.core.y) << std::right << std::setw(5) << r.completed_iterations
                  << std::setw(7) << r.stale << std::setw(8) << r.foreign << "  " << tag << std::endl;
        for (int s = 0; s < r.num_samples; s++) {
            const ForeignSample& sample = r.samples[s];
            std::cout << "    read@" << std::setw(3) << sample.iteration << " -> worker " << std::setw(2)
                      << sample.got.worker_id << " " << core_str(sample.got.noc_x, sample.got.noc_y) << " write@"
                      << sample.got.iteration << std::endl;
        }

        total_stale += r.stale;
        total_foreign += r.foreign;
        total_iterations += r.completed_iterations;
    }

    std::cout << "\ntotal: " << total_stale << " stale, " << total_foreign << " foreign / " << total_iterations
              << " inits" << std::endl;

    pthread_barrier_destroy(start_barrier);
    munmap(barrier_mem, sizeof(pthread_barrier_t));
    munmap(results_mem, sizeof(WorkerResult) * NUM_WORKERS);

    EXPECT_EQ(total_foreign, 0) << total_foreign << " DMA reads returned another core's data (see log above)";
    EXPECT_EQ(total_stale, 0) << total_stale << " DMA reads returned a stale value (see log above)";
}
