// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Safe-I/O warm-reset tests.
//
// KMD invalidates a device's PCIe mappings when it resets the device, so any host dereference of
// those mappings afterwards raises SIGBUS. The safe API (TTDevice::create(..., use_safe_api=true))
// exists to turn that into a catchable error: it installs a process-wide SIGBUS handler and puts
// every window into IoSafety::Enabled, where each operation runs under a sigsetjmp guard that
// converts the signal into SigbusError.
//
// In every test the warm reset is performed from a context that is not one of the I/O participants:
// a dedicated thread, a dedicated process, or (for the timing sweep) a worker racing this thread.
//
// SafeApiHandlesReset genuinely races the reset against in-flight I/O (see its own comment for why
// that is the right thing for that one test to do). The rest wait for the reset to finish before
// issuing any I/O, which makes them deterministic: the mapping is already dead when the first
// transfer is issued, so every transfer must come back as SigbusError with nothing left to timing.
// Both shapes exercise the same fix, from different angles: the reconfigure preceding every transfer
// is the first host access of that transfer, so racing the reset can land the fault inside it, and
// waiting for the reset guarantees the fault lands there. While configure() sat outside the guard, a
// SIGBUS landing there reached the handler with no jump target set and silently _exit()ed the
// process.
//
// The deterministic tests then repeat the transfer several times per participant, because
// execute_safe has to re-arm its thread-local jump target after longjmp-ing out of the previous
// fault: a second fault that found the guard disarmed would kill the process instead of throwing.
//
// These tests reset the board, so they live in the on-demand/nightly destructive_tests target
// rather than in api_tests, which runs on every PR.

#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "device/api/umd/device/warm_reset.hpp"
#include "device/api/umd/device/warm_reset_with_recovery.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/multi_process_event.hpp"
#include "tests/test_utils/pipe_communication.hpp"
#include "tests/test_utils/process_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/kmd_versions.hpp"
#include "umd/device/utils/semver.hpp"
#include "utils.hpp"

using namespace tt;
using namespace tt::umd;

namespace {

// A warm reset takes seconds (link retrain, settle delay, device re-probe), so waiting on one gets a
// generous budget. It is a backstop only: the reset reports its own failure through its return value.
constexpr auto RESET_WAIT_TIMEOUT = std::chrono::seconds(180);

// How long the racing hammer loop below waits for the reset to invalidate its mapping. The
// invalidation happens inside the reset's own ioctls, long before warm_reset() returns, so a
// healthy run trips in well under a second; this only bounds a run where it never happens at all.
constexpr auto SIGBUS_RACE_TIMEOUT = std::chrono::seconds(60);

using Payload = std::array<uint32_t, 10>;

constexpr Payload IO_PAYLOAD = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
constexpr size_t IO_BYTES = IO_PAYLOAD.size() * sizeof(uint32_t);

// How many write+read rounds each participant issues once the mapping is dead. More than one so the
// guard is proven to re-arm after longjmp-ing out of the previous fault.
constexpr int TRANSFER_ROUNDS_AFTER_RESET = 3;

// Every transfer of every round has to fault: a write and a read per round.
constexpr int EXPECTED_SIGBUS_PER_PARTICIPANT = TRANSFER_ROUNDS_AFTER_RESET * 2;

// Passive wait for a flag another thread sets, backing off between checks.
template <typename Predicate>
bool wait_for_condition(Predicate predicate, const std::chrono::milliseconds timeout) {
    static constexpr auto BUSY_POLL_WINDOW = std::chrono::microseconds(0);
    static constexpr auto POLL_INTERVAL = std::chrono::microseconds(1'000);
    return utils::poll_until(predicate, timeout, BUSY_POLL_WINDOW, POLL_INTERVAL);
}

// Galaxy (UBB) boards are reset over IPMI by ubb_warm_reset(), not by the PCIe-level warm_reset()
// these tests drive, so they have to be skipped there. Probed from a bare TTDevice rather than from
// a Cluster's descriptor: the board type needs one initialised device, whereas building a Cluster
// runs a full topology discovery.
bool is_galaxy_board(int pci_device_id) {
    auto tt_device = TTDevice::create(pci_device_id);
    tt_device->init_tt_device();
    return is_galaxy_board_type(tt_device->get_board_type());
}

}  // namespace

// Owns the skip conditions, the safe-API handles, and the post-reset health check shared by every
// test below. Each test resets the board, so the health check matters: without it a failed recovery
// would surface as an unrelated failure in whatever runs next in this binary.
class SafeIoWarmResetTest : public ::testing::Test {
protected:
    std::vector<int> pci_device_ids_;
    std::map<int, std::unique_ptr<TTDevice>> tt_devices_;
    CoreCoord tensix_core_;

    // Set by a test once it has triggered a reset, so TearDown knows the board needs checking.
    bool reset_issued_ = false;

    void SetUp() override {
        if (utils::is_arm_platform()) {
            GTEST_SKIP() << "Warm reset is disabled on ARM64 due to instability.";
        }

        pci_device_ids_ = PCIDevice::enumerate_devices();
        if (pci_device_ids_.empty()) {
            GTEST_SKIP() << "No PCI devices found.";
        }

        // Cached: the board cannot change between tests in one binary, so the probe runs once here
        // rather than once for each of the tests in this file.
        static const bool galaxy_board = is_galaxy_board(pci_device_ids_.front());
        if (galaxy_board) {
            GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
        }

        // Everything below rests on KMD invalidating PCIe mappings on reset; older drivers leave a
        // stale mapping live, so nothing faults and a SigbusError-waiting test just times out. A
        // skip, not a hard fail: read_kmd_version() also reads back {0,0,0} with no module loaded.
        const SemVer kmd_version = PCIDevice::read_kmd_version();
        if (!(kmd_version >= KMD_RESET_MAPPING_INVALIDATION)) {
            GTEST_SKIP() << "KMD " << kmd_version.str() << " does not invalidate PCIe mappings on device reset; the "
                         << "safe-I/O warm-reset tests need " << KMD_RESET_MAPPING_INVALIDATION.str() << " or newer.";
        }
    }

    void TearDown() override {
        close_safe_devices();

        if (!reset_issued_) {
            return;
        }

        if (board_discoverable()) {
            return;
        }

        // A bare warm reset occasionally leaves a transient post-reset state (e.g. an ETH core that
        // never regains its heartbeat) which only another reset clears -- that is what
        // WarmResetWithRecovery retries for. It runs here, after the safe-API handles are closed,
        // rather than alongside the test's own reset: its topology discovery would otherwise run
        // while this process still holds pre-reset file descriptors.
        EXPECT_TRUE(WarmResetWithRecovery::warm_reset()) << "Board did not come back after the test's warm reset.";
        EXPECT_TRUE(board_discoverable()) << "No chips present after warm reset.";
    }

    // Opens one safe-API TTDevice per PCI device. use_safe_api is what installs the SIGBUS handler
    // and selects IoSafety::Enabled for the windows these devices do I/O through.
    void open_safe_devices() {
        for (int pci_device_id : pci_device_ids_) {
            auto tt_device = TTDevice::create(pci_device_id, IODeviceType::PCIe, /*use_safe_api=*/true);
            tt_device->set_power_state(TTDevice::PowerState::BUSY);
            tt_device->init_tt_device();
            tensix_core_ = tt_device->get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)[0];
            tt_devices_[pci_device_id] = std::move(tt_device);
        }
    }

    // Restores SIG_DFL before dropping the handles: ~BlackholeTTDevice() does unguarded BAR2 writes
    // that can SIGBUS on a dead mapping, so clearing first would silently _exit() instead of crashing.
    void close_safe_devices() {
        TTDevice::set_sigbus_safe_handler(false);
        tt_devices_.clear();
    }

    TTDevice* first_device() { return tt_devices_.at(pci_device_ids_.front()).get(); }

    // Proves the target core is reachable before the reset, so a later failure to see SigbusError
    // cannot be confused with I/O that never worked to begin with.
    void verify_io_works() {
        Payload readback{};
        for (auto& [pci_device_id, tt_device] : tt_devices_) {
            tt_device->write_to_device(IO_PAYLOAD.data(), tensix_core_, SAFE_IO_L1_ADDRESS, IO_BYTES);
            tt_device->read_from_device(readback.data(), tensix_core_, SAFE_IO_L1_ADDRESS, IO_BYTES);
            ASSERT_EQ(readback, IO_PAYLOAD) << "Safe-API I/O did not work on PCI device " << pci_device_id
                                            << " before the reset; the test would prove nothing.";
        }
    }

    // Issues TRANSFER_ROUNDS_AFTER_RESET write+read rounds against an already-invalidated mapping
    // and returns how many of those transfers raised SigbusError. Must be called with the reset
    // finished, so the answer has to be EXPECTED_SIGBUS_PER_PARTICIPANT. Anything other than
    // SigbusError propagates, so the caller reports it rather than counting it as a miss.
    static int count_sigbus_transfers(TTDevice* tt_device, const CoreCoord& core) {
        Payload readback{};
        int sigbus_count = 0;
        for (int round = 0; round < TRANSFER_ROUNDS_AFTER_RESET; ++round) {
            // Write and read reconfigure the window identically but end in different transfer
            // routines, so both directions are checked.
            try {
                tt_device->write_to_device(IO_PAYLOAD.data(), core, SAFE_IO_L1_ADDRESS, IO_BYTES);
            } catch (const error::SigbusError&) {
                ++sigbus_count;
            }
            try {
                tt_device->read_from_device(readback.data(), core, SAFE_IO_L1_ADDRESS, IO_BYTES);
            } catch (const error::SigbusError&) {
                ++sigbus_count;
            }
        }
        return sigbus_count;
    }

    // Writes and reads across every device on the host, in a loop, until a reset invalidates one of
    // their mappings or the deadline passes. Returns true the moment any device raises SigbusError --
    // once one mapping is dead the race this test is about has already resolved, and which device
    // wins is inherently non-deterministic, so nothing is gained by continuing to hammer the rest.
    // Any other exception propagates so the test reports it instead of retrying around it.
    bool hammer_until_sigbus() {
        Payload readback{};
        const auto deadline = std::chrono::steady_clock::now() + SIGBUS_RACE_TIMEOUT;
        while (std::chrono::steady_clock::now() < deadline) {
            for (auto& device_entry : tt_devices_) {
                TTDevice* tt_device = device_entry.second.get();
                try {
                    tt_device->write_to_device(IO_PAYLOAD.data(), tensix_core_, SAFE_IO_L1_ADDRESS, IO_BYTES);
                    tt_device->read_from_device(readback.data(), tensix_core_, SAFE_IO_L1_ADDRESS, IO_BYTES);
                } catch (const error::SigbusError&) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    // Topology has to be rediscovered after a reset; building a Cluster does that, so a non-empty
    // one is the cheapest proof the board is usable again.
    bool board_discoverable() {
        try {
            auto cluster = test_utils::make_default_test_cluster();
            return !cluster->get_target_device_ids().empty();
        } catch (const std::exception&) {
            return false;
        }
    }
};

// The reset genuinely races the I/O here: the timing sweep below (see INSTANTIATE_TEST_SUITE_P)
// puts the reset at different points relative to an ongoing hammer loop across every device on the
// host, so the fault can land anywhere -- including, at delay 0, inside the very first configure().
// Unlike the deterministic tests below, "prove it can happen at all" is the property under test, not
// "prove it always happens on this exact transfer"; see hammer_until_sigbus().
class SafeIoWarmResetParamTest : public SafeIoWarmResetTest, public ::testing::WithParamInterface<int> {};

TEST_P(SafeIoWarmResetParamTest, SafeApiHandlesReset) {
    open_safe_devices();
    ASSERT_NO_FATAL_FAILURE(verify_io_works());

    std::atomic<bool> reset_ok{false};

    // The reset runs on its own worker so it races transfers that are already in flight on this
    // thread, rather than being serialized behind them.
    reset_issued_ = true;
    std::thread reset_worker([&, delay_us = GetParam()]() {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
        try {
            reset_ok = WarmReset::warm_reset();
        } catch (const std::exception&) {
            // An uncaught exception here would call std::terminate() on this thread.
        }
    });

    bool caught_sigbus = false;
    try {
        caught_sigbus = hammer_until_sigbus();
    } catch (...) {
        // Join before rethrowing: reset_worker is still joinable here, and unwinding past a
        // joinable std::thread also calls std::terminate().
        reset_worker.join();
        throw;
    }

    reset_worker.join();

    EXPECT_TRUE(reset_ok.load()) << "The warm reset itself failed, so the safe-I/O path was never exercised.";
    EXPECT_TRUE(caught_sigbus) << "No SigbusError within " << SIGBUS_RACE_TIMEOUT.count()
                               << "s of the reset; the mapping was never invalidated.";
}

INSTANTIATE_TEST_SUITE_P(ResetTimingVariations, SafeIoWarmResetParamTest, ::testing::Values(0, 10, 50, 100, 500, 1000));

// Every thread doing I/O through the same device must get its own SigbusError: the jump target is
// thread-local, so one thread longjmp-ing out must not disturb the others, and none of them may be
// killed by the signal. The reset comes from a dedicated worker and the I/O threads hold off until
// it reports completion, so all of their transfers are against a mapping that is already dead.
TEST_F(SafeIoWarmResetTest, SafeApiMultiThreaded) {
    static constexpr int NUM_WORKERS = 2;

    open_safe_devices();
    ASSERT_NO_FATAL_FAILURE(verify_io_works());

    std::atomic<int> caught_sigbus{0};
    std::atomic<bool> reset_complete{false};
    std::atomic<bool> reset_ok{false};

    std::mutex unexpected_errors_mutex;
    std::vector<std::string> unexpected_errors;
    auto record_error = [&](const std::string& what) {
        // Collected rather than asserted from the worker: the main thread reports these after joining.
        std::lock_guard<std::mutex> lock(unexpected_errors_mutex);
        unexpected_errors.push_back(what);
    };

    auto worker = [&]() {
        try {
            // Wait for the reset to have finished, not merely started, so the first transfer below
            // is guaranteed to hit a dead mapping. An explicit handshake rather than a sleep long
            // enough to "probably" cover the reset.
            if (!wait_for_condition([&]() { return reset_complete.load(); }, RESET_WAIT_TIMEOUT)) {
                record_error("timed out waiting for the reset to complete before starting I/O");
                return;
            }
            caught_sigbus += count_sigbus_transfers(first_device(), tensix_core_);
        } catch (const std::exception& e) {
            record_error(e.what());
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(NUM_WORKERS);
    for (int i = 0; i < NUM_WORKERS; ++i) {
        workers.emplace_back(worker);
    }

    reset_issued_ = true;
    std::thread reset_worker([&]() {
        try {
            reset_ok = WarmReset::warm_reset();
        } catch (const std::exception&) {
            // An uncaught exception here would call std::terminate() on this thread.
        }
        reset_complete = true;
    });

    for (auto& t : workers) {
        t.join();
    }
    reset_worker.join();

    const int caught = caught_sigbus.load();
    static constexpr int EXPECTED_SIGBUS_TOTAL = NUM_WORKERS * EXPECTED_SIGBUS_PER_PARTICIPANT;

    EXPECT_TRUE(reset_ok.load()) << "The warm reset itself failed, so the safe-I/O path was never exercised.";
    EXPECT_TRUE(unexpected_errors.empty())
        << "Worker threads saw " << unexpected_errors.size()
        << " unexpected error(s), first: " << (unexpected_errors.empty() ? "" : unexpected_errors[0]);
    EXPECT_EQ(caught, EXPECTED_SIGBUS_TOTAL)
        << "Worker threads reported " << caught << " of an expected " << EXPECTED_SIGBUS_TOTAL << " SigbusErrors.";
}

// One reset invalidates the mappings of every process attached to the link, and each must recover on
// its own rather than being killed by the signal. The parent is the separate context that resets;
// the children hold off until it says the reset is done before touching their own mappings.
TEST_F(SafeIoWarmResetTest, SafeApiMultiProcess) {
    static constexpr int NUM_CHILDREN = 3;
    static constexpr int CHILD_READY_TIMEOUT_S = 60;
    static constexpr int CHILD_REAP_TIMEOUT_S = 120;

    static constexpr int EXIT_SUCCESS_CODE = 0;        // every post-reset transfer raised SigbusError
    static constexpr int EXIT_MISSING_SIGBUS = 1;      // at least one transfer did not fault
    static constexpr int EXIT_WRONG_EXCEPTION = 2;     // something other than SigbusError came out
    static constexpr int EXIT_SETUP_FAILED = 3;        // never got as far as doing I/O
    static constexpr int EXIT_RESET_WAIT_TIMEOUT = 4;  // parent never reported the reset as done

    test_utils::MultiProcessPipe children_ready(NUM_CHILDREN);
    // One slot per child: an eventfd read consumes the notification, so they cannot share one.
    test_utils::MultiProcessEvent reset_done(NUM_CHILDREN);
    std::vector<pid_t> pids;

    for (int i = 0; i < NUM_CHILDREN; ++i) {
        pid_t pid = fork();
        if (pid == -1) {
            test_utils::terminate_processes(pids);
            FAIL() << "fork() failed for child " << i;
        }

        if (pid == 0) {
            // Child: its own handles and its own SIGBUS guard, sharing nothing with the parent --
            // which is the point, since the reset takes out every process's mapping at once.
            // _exit, not exit: a child must not run the parent's gtest teardown or atexit handlers.
            try {
                open_safe_devices();
            } catch (const std::exception&) {
                _exit(EXIT_SETUP_FAILED);
            }

            children_ready.signal_ready_from_child(i);

            if (!reset_done.wait_for(i, static_cast<int>(RESET_WAIT_TIMEOUT.count()))) {
                _exit(EXIT_RESET_WAIT_TIMEOUT);
            }

            int code = EXIT_SUCCESS_CODE;
            try {
                if (count_sigbus_transfers(first_device(), tensix_core_) != EXPECTED_SIGBUS_PER_PARTICIPANT) {
                    code = EXIT_MISSING_SIGBUS;
                }
            } catch (const std::exception&) {
                code = EXIT_WRONG_EXCEPTION;
            }
            _exit(code);
        }

        pids.push_back(pid);
    }

    if (!children_ready.wait_for_all_children(CHILD_READY_TIMEOUT_S)) {
        test_utils::terminate_processes(pids);
        FAIL() << "Timed out waiting for child processes to open their devices.";
    }

    reset_issued_ = true;
    const bool reset_ok = WarmReset::warm_reset();

    // Released unconditionally: on a failed reset the children must still run and report what they
    // saw rather than sitting on the handshake until their own timeout.
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        reset_done.notify(i);
    }

    auto describe = [](int rc) -> const char* {
        switch (rc) {
            case EXIT_MISSING_SIGBUS:
                return "did not report every post-reset transfer as SigbusError";
            case EXIT_WRONG_EXCEPTION:
                return "saw an exception other than SigbusError";
            case EXIT_SETUP_FAILED:
                return "failed to open its safe-API device";
            case EXIT_RESET_WAIT_TIMEOUT:
                return "timed out waiting for the reset to complete";
            case SIGBUS:
                // _exit(sig) from sigbus_handler()'s unguarded fallback: a normal exit, not WIFSIGNALED.
                return "took an unguarded SIGBUS (the safe-I/O guard was not in effect for that fault)";
            default:
                return "unknown failure";
        }
    };

    for (pid_t p : pids) {
        int status = 0;
        if (!test_utils::wait_for_child(p, &status, CHILD_REAP_TIMEOUT_S)) {
            test_utils::terminate_processes({p});
            ADD_FAILURE() << "Child " << p << " did not exit within " << CHILD_REAP_TIMEOUT_S
                          << "s; treating as a hang.";
            continue;
        }

        EXPECT_TRUE(WIFEXITED(status)) << "Child " << p << " was killed by a signal instead of catching SigbusError.";
        if (WIFEXITED(status) && WEXITSTATUS(status) != EXIT_SUCCESS_CODE) {
            ADD_FAILURE() << "Child " << p << " " << describe(WEXITSTATUS(status)) << " (exit code "
                          << WEXITSTATUS(status) << ").";
        }
    }

    EXPECT_TRUE(reset_ok) << "The warm reset itself failed, so the safe-I/O path was never exercised.";
}
