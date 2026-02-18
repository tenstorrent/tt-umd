// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/pipe_communication.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/exceptions.hpp"
#include "utils.hpp"

using namespace tt::umd;

// Small helper function to check if the ipmitool is ready.
bool is_ipmitool_ready() {
    if (system("which ipmitool > /dev/null 2>&1") != 0) {
        std::cout << "ipmitool executable not found." << std::endl;
        return false;
    }

    if ((access("/dev/ipmi0", F_OK) != 0) && (access("/dev/ipmi/0", F_OK) != 0) &&
        (access("/dev/ipmidev/0", F_OK) != 0)) {
        std::cout << "IPMI device file not found (/dev/ipmi0, /dev/ipmi/0, or /dev/ipmidev/0)." << std::endl;
        return false;
    }

    if (system("timeout 2 ipmitool power status > /dev/null 2>&1") != 0) {
        std::cout << "ipmitool power status command failed." << std::endl;
        return false;
    }

    return true;
}

// ============================================================================
// TTDevice Warm Reset Tests (moved from test_tt_device.cpp)
// ============================================================================

TEST(WarmResetTest, DISABLED_TTDeviceWarmResetAfterNocHang) {
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
    // EXPECT_NO_THROW(cluster->get_chip(0)->get_tt_device()->detect_hang_read());.

    tt_device.reset();

    tt_device = TTDevice::create(pci_device_ids.at(0));
    tt_device->init_tt_device();

    tt_device->write_to_device(zero_data.data(), tensix_core, address, zero_data.size());

    tt_device->write_to_device(data.data(), tensix_core, address, data.size());

    tt_device->read_from_device(readback_data.data(), tensix_core, address, readback_data.size());

    ASSERT_EQ(data, readback_data);
}

bool verify_data(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual, int device_id) {
    if (expected.size() != actual.size()) {
        std::cerr << "Device " << device_id << ": Size mismatch! Expected " << expected.size() << " but got "
                  << actual.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < expected.size(); i++) {
        if (expected[i] != actual[i]) {
            std::cerr << "Device " << device_id << ": Data mismatch at index " << i << "! Expected " << expected[i]
                      << " but got " << actual[i] << std::endl;
            return false;
        }
    }
    return true;
}

class WarmResetParamTest : public ::testing::TestWithParam<int> {};

// This test is currently disabled pending kernel driver support for mapping invalidation during resets.
// The test will be enabled once the kernel driver properly invalidates PCIe mappings when a warm
// reset occurs, allowing user-space to detect and handle the invalidation gracefully.
TEST_P(WarmResetParamTest, DISABLED_SafeApiHandlesReset) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    int delay_us = GetParam();
    std::atomic<bool> sigbus_caught{false};

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    std::map<int, std::unique_ptr<TTDevice>> tt_devices;

    tt_xy_pair tensix_core;

    for (int pci_device_id : pci_device_ids) {
        tt_devices[pci_device_id] = TTDevice::create(pci_device_id, IODeviceType::PCIe, true);

        tt_devices[pci_device_id]->init_tt_device();

        ChipInfo chip_info = tt_devices[pci_device_id]->get_chip_info();

        SocDescriptor soc_desc(tt_devices[pci_device_id]->get_arch(), chip_info);

        tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    }

    std::thread background_reset_thread([&]() {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
        WarmReset::warm_reset();
    });

    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(5);

    try {
        while (!sigbus_caught) {
            if (std::chrono::steady_clock::now() - start_time > timeout) {
                break;
            }

            for (int i = 0; i < 100; ++i) {
                for (int pci_device_id : pci_device_ids) {
                    tt_devices[pci_device_id]->write_to_device(
                        data_write.data(), tensix_core, address, data_write.size() * sizeof(uint32_t));

                    tt_devices[pci_device_id]->read_from_device(
                        data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));

                    verify_data(data_write, data_read, pci_device_id);

                    data_read = std::vector<uint32_t>(data_write.size(), 0);
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    } catch (const SigbusError& e) {
        sigbus_caught = true;
    } catch (const std::exception& e) {
        if (background_reset_thread.joinable()) {
            background_reset_thread.join();
        }
        FAIL() << "Caught unexpected exception: " << e.what();
    }

    if (background_reset_thread.joinable()) {
        background_reset_thread.join();
    }

    if (sigbus_caught) {
        SUCCEED() << "Successfully triggered SIGBUS within timeout.";
    } else {
        FAIL() << "Timed out after 5 seconds without hitting SIGBUS. Reset did not invalidate mappings in time.";
    }
}

INSTANTIATE_TEST_SUITE_P(ResetTimingVariations, WarmResetParamTest, ::testing::Values(0, 10, 50, 100, 500, 1000));

// This test is currently disabled pending kernel driver support for mapping invalidation during resets.
// The test will be enabled once the kernel driver properly invalidates PCIe mappings when a warm
// reset occurs, allowing user-space to detect and handle the invalidation gracefully.
TEST(WarmResetTest, DISABLED_SafeApiMultiThreaded) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    uint64_t address = 0x0;
    std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<uint32_t> data_read(data_write.size(), 0);
    std::map<int, std::unique_ptr<TTDevice>> tt_devices;

    tt_xy_pair tensix_core;

    for (int pci_device_id : pci_device_ids) {
        tt_devices[pci_device_id] = TTDevice::create(pci_device_id, IODeviceType::PCIe, true);

        tt_devices[pci_device_id]->init_tt_device();

        ChipInfo chip_info = tt_devices[pci_device_id]->get_chip_info();

        SocDescriptor soc_desc(tt_devices[pci_device_id]->get_arch(), chip_info);

        tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
    }

    std::atomic<int> caught_sigbus{0};

    auto worker = [&](int id) {
        try {
            // This thread hammers the device and waits for the reset to kill it.
            while (true) {
                tt_devices[pci_device_ids[0]]->read_from_device(
                    data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        } catch (const SigbusError& e) {
            caught_sigbus++;
        } catch (const std::exception& e) {
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);

    // Trigger the reset after a small delay.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    WarmReset::warm_reset();

    t1.join();
    t2.join();

    EXPECT_EQ(caught_sigbus, 2);
}

// This test is currently disabled pending kernel driver support for mapping invalidation during resets.
// The test will be enabled once the kernel driver properly invalidates PCIe mappings when a warm
// reset occurs, allowing user-space to detect and handle the invalidation gracefully.
TEST(WarmResetTest, DISABLED_SafeApiMultiProcess) {
    std::vector<int> pci_device_ids = PCIDevice::enumerate_devices();

    if (pci_device_ids.empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    constexpr int NUM_CHILDREN = 3;
    utils::MultiProcessPipe pipes(NUM_CHILDREN);
    std::vector<pid_t> pids;

    for (int i = 0; i < NUM_CHILDREN; ++i) {
        pid_t pid = fork();
        if (pid == 0) {  // Child Process

            uint64_t address = 0x0;
            std::vector<uint32_t> data_write = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            std::vector<uint32_t> data_read(data_write.size(), 0);
            std::map<int, std::unique_ptr<TTDevice>> tt_devices;

            tt_xy_pair tensix_core;

            for (int pci_device_id : pci_device_ids) {
                tt_devices[pci_device_id] = TTDevice::create(pci_device_id, IODeviceType::PCIe, true);

                tt_devices[pci_device_id]->init_tt_device();

                ChipInfo chip_info = tt_devices[pci_device_id]->get_chip_info();

                SocDescriptor soc_desc(tt_devices[pci_device_id]->get_arch(), chip_info);

                tensix_core = soc_desc.get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
            }

            pipes.signal_ready_from_child(i);

            try {
                // The "Hammer" loop.
                while (true) {
                    tt_devices[pci_device_ids[0]]->read_from_device(
                        data_read.data(), tensix_core, address, data_read.size() * sizeof(uint32_t));
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            } catch (const SigbusError& e) {
                std::exit(0);  // Success: SIGBUS was isolated and caught
            } catch (const std::exception& e) {
                std::exit(1);  // Error: Wrong exception
            }
            std::exit(2);  // Error: Timed out/Loop exited without signal
        }
        pids.push_back(pid);
    }

    pipes.wait_for_all_children(20);

    // Parent triggers the reset that affects ALL windows on that PCIe link.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WarmReset::warm_reset();

    for (pid_t p : pids) {
        int status;
        waitpid(p, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Child process " << p << " failed.";
    }
}

// ============================================================================
// Cluster Warm Reset Tests (moved from test_cluster.cpp)
// ============================================================================

TEST(WarmResetTest, DISABLED_ClusterWarmResetScratch) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    uint32_t write_test_data = 0xDEADBEEF;

    auto chip_id = *cluster->get_target_device_ids().begin();
    auto tt_device = cluster->get_chip(chip_id)->get_tt_device();

    tt_device->bar_write32(
        tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
            tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset(),
        write_test_data);

    WarmReset::warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();
    chip_id = *cluster->get_target_device_ids().begin();
    tt_device = cluster->get_chip(chip_id)->get_tt_device();

    auto read_test_data = tt_device->bar_read32(
        tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
        tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset());

    EXPECT_NE(write_test_data, read_test_data);
}

TEST(WarmResetTest, GalaxyWarmResetScratch) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    static constexpr uint32_t DEFAULT_VALUE_IN_SCRATCH_REGISTER = 0;

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (!is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Only galaxy test configuration.";
    }

    auto arch = cluster->get_cluster_description()->get_arch();
    if (arch != tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP() << "Only test Wormhole architecture for Galaxy UBB reset.";
    }

    if (!is_ipmitool_ready()) {
        GTEST_SKIP() << "Only test warm reset on systems that have the ipmi tool.";
    }

    static constexpr uint32_t write_test_data = 0xDEADBEEF;

    for (auto& chip_id : cluster->get_target_mmio_device_ids()) {
        auto tt_device = cluster->get_chip(chip_id)->get_tt_device();
        tt_device->bar_write32(
            tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
                tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset(),
            write_test_data);
    }

    WarmReset::ubb_warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();

    for (auto& chip_id : cluster->get_target_mmio_device_ids()) {
        auto tt_device = cluster->get_chip(chip_id)->get_tt_device();

        auto read_test_data = tt_device->bar_read32(
            tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
            tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset());

        EXPECT_NE(write_test_data, read_test_data);
        EXPECT_EQ(DEFAULT_VALUE_IN_SCRATCH_REGISTER, read_test_data);
    }
}

TEST(WarmResetTest, ClusterWarmReset) {
    if constexpr (is_arm_platform()) {
        GTEST_SKIP() << "Warm reset is disabled on ARM64 due to instability.";
    }
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    auto arch = cluster->get_tt_device(0)->get_arch();
    if (arch == tt::ARCH::WORMHOLE_B0) {
        GTEST_SKIP()
            << "This test intentionally hangs the NOC. On Wormhole, this can cause a severe failure where even a warm "
               "reset does not recover the device, requiring a watchdog-triggered reset for recovery.";
    }

    std::vector<uint8_t> data{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> zero_data(data.size(), 0);
    std::vector<uint8_t> readback_data(data.size(), 0);

    // send data to core 15, 15 which will hang the NOC
    auto hanged_chip_id = *cluster->get_target_device_ids().begin();
    auto hanged_tt_device = cluster->get_chip(hanged_chip_id)->get_tt_device();
    hanged_tt_device->write_to_device(data.data(), {15, 15}, 0, data.size());

    // TODO: Remove this check when it is figured out why there is no hang detected on Blackhole.
    if (arch == tt::ARCH::WORMHOLE_B0) {
        EXPECT_THROW(hanged_tt_device->detect_hang_read(), std::runtime_error);
    }

    WarmReset::warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();

    EXPECT_FALSE(cluster->get_target_device_ids().empty()) << "No chips present after reset.";

    // TODO: Comment this out after finding out how to detect hang reads on
    // EXPECT_NO_THROW(cluster->get_chip(0)->get_tt_device()->detect_hang_read());.

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            RiscType select_all_tensix_riscv_cores{RiscType::ALL_TENSIX};

            // Set all riscs to reset state.
            cluster->assert_risc_reset(chip_id, tensix_core, select_all_tensix_riscv_cores);

            cluster->l1_membar(chip_id, {tensix_core});

            // Zero out first 8 bytes on L1.
            cluster->write_to_device(zero_data.data(), zero_data.size(), chip_id, tensix_core, 0);

            cluster->write_to_device(data.data(), data.size(), chip_id, tensix_core, 0);

            cluster->read_from_device(readback_data.data(), chip_id, tensix_core, 0, readback_data.size());

            ASSERT_EQ(data, readback_data);
        }
    }
}

// ============================================================================
// Warm Reset Notification Tests (moved from test_notification_mechanism.cpp)
// ============================================================================

class WarmResetNotificationTest : public ::testing::Test {
public:
    static int run_child_monitor_logic(
        std::chrono::seconds process_pre_notification_wait_time = std::chrono::seconds(4),
        std::chrono::seconds process_post_notification_wait_time = std::chrono::seconds(4),
        const std::function<void()>& on_started = nullptr) {
        std::promise<void> pre_reset_promise;
        std::promise<void> post_reset_promise;
        auto pre_future = pre_reset_promise.get_future();
        auto post_future = post_reset_promise.get_future();

        bool success = WarmResetCommunication::Monitor::start_monitoring(
            [&]() { pre_reset_promise.set_value(); }, [&]() { post_reset_promise.set_value(); });

        if (!success) {
            return 1;
        }

        // Used only in WarmResetProcessWaitTest for testing unfulfilled promises.
        if (on_started) {
            on_started();
        }

        // Wait for PRE.
        if (pre_future.wait_for(process_pre_notification_wait_time) != std::future_status::ready) {
            return 101;  // Code 101: Pre Timeout
        }

        // Wait for POST.
        if (post_future.wait_for(process_post_notification_wait_time) != std::future_status::ready) {
            return 102;  // Code 102: Post Timeout
        }

        return 0;  // Success
    }

protected:
    void SetUp() override {
        // Clean the slate before every test.
        std::error_code ec;
        std::filesystem::remove_all(WarmResetCommunication::LISTENER_DIR, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(WarmResetCommunication::LISTENER_DIR, ec);
    }

    void wait_for_socket_state(int pid, bool should_exist) {
        std::string socket_name = "client_" + std::to_string(pid) + ".sock";
        std::filesystem::path socket_path = std::filesystem::path(WarmResetCommunication::LISTENER_DIR) / socket_name;

        int retries = 50;  // Wait up to 500ms.
        while (retries--) {
            bool currently_exists = std::filesystem::exists(socket_path);

            // If the current state matches the desired state, we are done.
            if (currently_exists == should_exist) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        FAIL() << "Timeout waiting for socket " << socket_path << " to "
               << (should_exist ? "appear (Creation)" : "vanish (Removal)");
    }
};

class WarmResetTimingTest : public WarmResetNotificationTest, public testing::WithParamInterface<int> {};

INSTANTIATE_TEST_SUITE_P(
    TimeoutScenarios,
    WarmResetTimingTest,
    ::testing::Values(
        100,  // Case 1: Fast (No Timeout)
        2000  // Case 2: Slow (Timeout)
        ),
    [](const testing::TestParamInfo<int>& info) {
        return info.param == 100 ? "FastSequence" : "SlowSequenceWithTimeout";
    });

TEST_P(WarmResetTimingTest, MultiProcess) {
    auto constexpr NUM_CHILDREN = 5;
    std::vector<pid_t> child_pids;

    for (int i = 0; i < NUM_CHILDREN; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            int result = WarmResetNotificationTest::run_child_monitor_logic();
            _exit(result);
        }
        ASSERT_GT(pid, 0);
        child_pids.push_back(pid);
    }

    // Allow startup of processes.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::chrono::milliseconds sleep_duration_ms = std::chrono::milliseconds(GetParam());

    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(1000));
    std::this_thread::sleep_for(sleep_duration_ms);
    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    // Verify.
    for (pid_t pid : child_pids) {
        int status;
        waitpid(pid, &status, 0);
        ASSERT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(WarmResetNotificationTest, NotifierIgnoresStaleSockets) {
    std::error_code ec;
    std::filesystem::create_directories(WarmResetCommunication::LISTENER_DIR, ec);

    // Create a fake socket file (just a regular empty file, or a bound socket with no listener)
    // Let's make it tricky: A file that looks like a socket name but is just a file.
    std::string fake_socket = std::string(WarmResetCommunication::LISTENER_DIR) + "/client_99999.sock";
    std::ofstream ofs(fake_socket);
    ofs.close();

    // Run Notifier
    // If code is fragile, this might throw an exception or hang.
    EXPECT_NO_THROW(
        { WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(100)); });

    EXPECT_NO_THROW({ WarmResetCommunication::Notifier::notify_all_listeners_post_reset(); });
}

TEST_F(WarmResetNotificationTest, ResilientToClientFailure) {
    pid_t good_pid = fork();
    if (good_pid == 0) {
        // This client behaves nicely.
        _exit(run_child_monitor_logic());
    }

    pid_t bad_pid = fork();
    if (bad_pid == 0) {
        // This client starts monitoring but then crashes/exits immediately
        // leaving a valid socket file but no process reading it.
        WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        _exit(1);  // Die unexpectedly
    }

    // Give time for setup and for bad_pid to die.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // The Bad PID's socket is likely still there (OS cleanup might lag or file persists),
    // but connection will be refused. Notifier must survive.
    WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(500));
    WarmResetCommunication::Notifier::notify_all_listeners_post_reset();

    // Verify Good PID succeeded.
    int status;
    waitpid(good_pid, &status, 0);
    EXPECT_EQ(WEXITSTATUS(status), 0);

    // Cleanup bad pid (already dead).
    waitpid(bad_pid, &status, 0);
}

TEST_F(WarmResetNotificationTest, MonitorCanRestart) {
    bool first_valid_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_TRUE(first_valid_start);

    wait_for_socket_state(getpid(), true);

    bool double_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_FALSE(double_start);

    WarmResetCommunication::Monitor::stop_monitoring();

    wait_for_socket_state(getpid(), false);

    bool second_valid_start = WarmResetCommunication::Monitor::start_monitoring([]() {}, []() {});
    ASSERT_TRUE(second_valid_start);

    wait_for_socket_state(getpid(), true);

    WarmResetCommunication::Monitor::stop_monitoring();
}

struct TimeoutParams {
    std::chrono::milliseconds pre_wait;
    std::chrono::milliseconds post_wait;
    int expected_rc;
    bool should_trigger_pre;  // Needed to reach the "Post" check
};

class WarmResetProcessWaitTest : public WarmResetNotificationTest, public testing::WithParamInterface<TimeoutParams> {
    void TearDown() override {
        WarmResetCommunication::Monitor::stop_monitoring();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        WarmResetNotificationTest::TearDown();
    }
};

INSTANTIATE_TEST_SUITE_P(
    TimeoutScenarios,
    WarmResetProcessWaitTest,
    ::testing::Values(
        // Case 1: Pre Timeout (101).
        // Wait 1ms for Pre. Don't send signal. Fails immediately.
        TimeoutParams{std::chrono::milliseconds(1), std::chrono::seconds(1), 101, false},

        // Case 2: Post Timeout (102).
        // Wait 2s for Pre (success), 1ms for Post (fail). Send Pre signal only.
        TimeoutParams{std::chrono::seconds(2), std::chrono::milliseconds(1), 102, true}),
    [](const testing::TestParamInfo<TimeoutParams>& info) {
        return info.param.expected_rc == 101 ? "PreTimeout_101" : "PostTimeout_102";
    });

TEST_P(WarmResetProcessWaitTest, ValidatesTimeoutLogic) {
    auto params = GetParam();
    tt::umd::test_utils::MultiProcessPipe pipe(1);

    pid_t pid = fork();

    if (pid == 0) {
        int result = run_child_monitor_logic(
            std::chrono::duration_cast<std::chrono::seconds>(params.pre_wait),
            std::chrono::duration_cast<std::chrono::seconds>(params.post_wait),
            [&]() { pipe.signal_ready_from_child(0); });
        _exit(result);
    }

    ASSERT_TRUE(pipe.wait_for_all_children(5));

    if (params.should_trigger_pre) {
        // SAFETY CHECK: Ensure the background thread actually created the socket.
        // This is much better than a hardcoded sleep.
        std::string socket_path =
            std::string(WarmResetCommunication::LISTENER_DIR) + "/client_" + std::to_string(pid) + ".sock";

        // Quick spin-wait (usually exits instantly).
        while (!std::filesystem::exists(socket_path)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Now we are 100% sure the listener is active.
        WarmResetCommunication::Notifier::notify_all_listeners_pre_reset(std::chrono::milliseconds(500));
    }

    int status;
    waitpid(pid, &status, 0);

    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), params.expected_rc);
}
