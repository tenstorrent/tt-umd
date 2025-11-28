// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <thread>

#include "device/api/umd/device/warm_reset.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "tt-logger/tt-logger.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "utils.hpp"

using namespace tt::umd;

TEST(ApiTTDeviceTest, TestAsio) { check_asio_version(); }

TEST(ApiTTDeviceTest, ListenAsio) {
    WarmReset::start_monitoring(
        []() { log_info(tt::LogUMD, "Set pre read_device to false"); },
        []() { log_info(tt::LogUMD, "Set post read_device to false"); },
        []() { log_info(tt::LogUMD, "Set read_device to true"); });
    while (1) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    };
}

TEST(ApiTTDeviceTest, NotifyAsio) {
    WarmReset::notify_all_listeners_with_handshake(std::chrono::milliseconds(5'0000));
    std::cout << "Managed to do this\n";
    sleep(5);
    WarmReset::notify_all_listeners_post_reset();
}

TEST(ApiTTDeviceTest, EndlessIO) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    std::vector<std::unique_ptr<TTDevice>> tt_devices(pci_device_ids.size());
    for (int pci_device_id : pci_device_ids) {
        tt_devices[pci_device_id] = TTDevice::create(pci_device_id);
        tt_devices[pci_device_id]->init_tt_device();
    }

    while (1) {
        for (int pci_device_id : pci_device_ids) {
            ChipInfo chip_info = tt_devices[pci_device_id]->get_chip_info();

            SocDescriptor soc_desc(tt_devices[pci_device_id]->get_arch(), chip_info);

            tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

            tt_devices[pci_device_id]->write_to_device(
                data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

            tt_devices[pci_device_id]->read_from_device(
                data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

            ASSERT_EQ(data_write, data_read);

            data_read = std::vector<uint32_t>(data_write.size(), 0);
        }
        log_info(tt::LogUMD, "New read cycle");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

TEST(ApiTTDeviceTest, TestDummyLongJump) {
    std::unique_ptr<TTDeviceDummy> tt_device = std::make_unique<TTDeviceDummy>();

    // Mutex for serializing console output
    std::mutex cout_mutex;

    // Thread ID that will be used by the main thread to send SIGBUS
    std::atomic<pthread_t> working_thread_id{0};
    std::atomic<pthread_t> blocked_thread_id{0};
    std::atomic<pthread_t> unsafe_thread_id{0};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> signal_sent{false};
    std::atomic<bool> exception_caught{false};

    // Worker thread 1: calls dummy_safe() and gets stuck in infinite loop

    std::thread worker_thread([&]() {
        working_thread_id.store(pthread_self());
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Worker TID: " << pthread_self() << std::endl;
        }
        worker_started.store(true);

        try {
            tt_device->dummy_safe();
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "SIGBUS");
            exception_caught.store(true);
        }
    });

    // Worker thread 2: tries to call dummy_safe() but will block on the mutex
    std::thread blocked_thread([&]() {
        blocked_thread_id.store(pthread_self());
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Blocked Safe TID: " << pthread_self() << std::endl;
        }
        // Wait for worker thread to be inside dummy_safe
        while (!worker_started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Give worker thread time to acquire the lock
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try {
            // This should block until worker thread releases the lock
            tt_device->dummy_safe();
        } catch (const std::runtime_error& e) {
            // This thread might not reach here if test ends
        }
    });

    // Worker thread 3: calls dummy() (NOT safe) - no jump_set flag, will crash if it receives SIGBUS
    std::thread unsafe_thread([&]() {
        unsafe_thread_id.store(pthread_self());
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Blocked Unsafe TID: " << pthread_self() << std::endl;
        }
        // Wait for worker thread to be inside dummy_safe
        while (!worker_started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Give worker thread time to acquire the lock
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try {
            // This should block until worker thread releases the lock
            // If this thread receives SIGBUS while blocked, it will crash (jump_set is false)
            tt_device->dummy();
        } catch (const std::runtime_error& e) {
            std::cout << "Unsafe thread caught exception: " << e.what() << std::endl;
        }
    });

    // Main thread: wait for worker to start, then send SIGBUS
    while (!worker_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Give worker thread time to be well into the infinite loop
    std::this_thread::sleep_for(std::chrono::milliseconds(5'000));

    pthread_t target_thread = working_thread_id.load();
    ASSERT_NE(target_thread, (pthread_t)0) << "Worker thread ID not set";

    // Send SIGBUS to the working thread
    int result = pthread_kill(target_thread, SIGBUS);
    ASSERT_EQ(result, 0) << "pthread_kill failed";
    signal_sent.store(true);

    // Wait for worker thread to catch the exception and exit
    worker_thread.join();

    // Verify the exception was caught
    EXPECT_TRUE(exception_caught.load()) << "Worker thread did not catch SIGBUS exception";

    // Clean up blocked threads (they should be able to proceed now or we'll terminate them)
    blocked_thread.detach();
    unsafe_thread.detach();

    std::cout << "Test completed successfully: SIGBUS was caught and handled" << std::endl;
    std::cout << "Unsafe thread ID was: " << unsafe_thread_id.load() << " (did not receive SIGBUS)" << std::endl;
}

TEST(ApiTTDeviceTest, TestLongJump) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    std::unique_ptr<TTDevice> tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    // Mutex for serializing console output
    std::mutex cout_mutex;

    std::vector<uint8_t> buffer(1024 * 1024, 0);
    ChipInfo chip_info = tt_device->get_chip_info();
    SocDescriptor soc_desc(tt_device->get_arch(), chip_info);
    tt_xy_pair tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];

    // Thread ID that will be used by the main thread to send SIGBUS
    std::atomic<pthread_t> working_thread_id{0};
    std::atomic<pthread_t> blocked_thread_id{0};
    std::atomic<pthread_t> unsafe_thread_id{0};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> signal_sent{false};
    std::atomic<bool> exception_caught{false};

    // Worker thread 1: calls read_from_device() and gets stuck in a long loop

    std::thread worker_thread([&]() {
        working_thread_id.store(pthread_self());
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Worker TID: " << pthread_self() << std::endl;
        }
        worker_started.store(true);

        try {
            tt_device->read_from_device(buffer.data(), tensix_core, 0x0, buffer.size());
        } catch (const std::runtime_error& e) {
            EXPECT_STREQ(e.what(), "SIGBUS");
            exception_caught.store(true);
        }
    });

    // Worker thread 2: tries to call dummy_safe() but will block on the mutex
    std::thread blocked_thread([&]() {
        blocked_thread_id.store(pthread_self());
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Blocked Safe TID: " << pthread_self() << std::endl;
        }
        // Wait for worker thread to be inside dummy_safe
        while (!worker_started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Give worker thread time to acquire the lock
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try {
            // This should block until worker thread releases the lock
            tt_device->read_from_device(buffer.data(), tensix_core, 0x0, buffer.size());
        } catch (const std::runtime_error& e) {
            // This thread might not reach here if test ends
        }
    });

    // Worker thread 3: calls dummy() (NOT safe) - no jump_set flag, will crash if it receives SIGBUS
    std::thread unsafe_thread([&]() {
        unsafe_thread_id.store(pthread_self());
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Blocked Unsafe TID: " << pthread_self() << std::endl;
        }
        // Wait for worker thread to be inside dummy_safe
        while (!worker_started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Give worker thread time to acquire the lock
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try {
            // This should block until worker thread releases the lock
            // If this thread receives SIGBUS while blocked, it will crash (jump_set is false)
            tt_device->read_from_device(buffer.data(), tensix_core, 0x0, buffer.size());
        } catch (const std::runtime_error& e) {
            std::cout << "Unsafe thread caught exception: " << e.what() << std::endl;
        }
    });

    // Main thread: wait for worker to start, then send SIGBUS
    while (!worker_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Give worker thread time to be well into the infinite loop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    pthread_t target_thread = working_thread_id.load();
    ASSERT_NE(target_thread, (pthread_t)0) << "Worker thread ID not set";

    // Send SIGBUS to the working thread
    int result = pthread_kill(target_thread, SIGBUS);
    ASSERT_EQ(result, 0) << "pthread_kill failed";
    signal_sent.store(true);

    // Wait for worker thread to catch the exception and exit
    worker_thread.join();

    // Verify the exception was caught
    EXPECT_TRUE(exception_caught.load()) << "Worker thread did not catch SIGBUS exception";

    // Clean up blocked threads (they should be able to proceed now or we'll terminate them)
    blocked_thread.detach();
    unsafe_thread.detach();

    std::cout << "Test completed successfully: SIGBUS was caught and handled" << std::endl;
    std::cout << "Unsafe thread ID was: " << unsafe_thread_id.load() << " (did not receive SIGBUS)" << std::endl;
}

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

TEST(ApiTTDeviceTest, TTDeviceWarmResetAfterNocHang) {
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
