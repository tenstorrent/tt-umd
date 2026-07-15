// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Microbenchmarks comparing the available UMD locking backends:
//   - RobustMutex     : pthread robust mutex in a /dev/shm file (userspace fast path).
//   - KmdMutex        : KMD resource lock via ioctl (syscall per op, tied to a silicon device).
//
// FlockMutex (flock(2) over a /dev/shm file) was a third backend; its implementation has been
// removed, but its benchmark wiring is kept commented out below so it can be re-enabled easily.
//
// What is measured, per mechanism:
//   - Initialize        : construct + initialize() + destroy.
//   - LockUnlock         : uncontended lock() + unlock() on one thread.
//   - ProbeContended     : probe_lock() cost when the lock is already held (returns "busy").
//   - LockUnlockThreads  : aggregate lock()/unlock() throughput under N contending threads.
//
// KmdMutex benchmarks are skipped when no /dev/tenstorrent device is present. They use a high
// resource-lock index that does not collide with ERISC (0..15) or the device-exclusive convention.

#include <gtest/gtest.h>
#include <nanobench.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/pcie/pci_device.hpp"
// #include "umd/device/utils/flock_mutex.hpp"  // FlockMutex removed; see note at top of file.
#include "umd/device/utils/kmd_mutex.hpp"
#include "umd/device/utils/robust_mutex.hpp"

using namespace tt::umd;

namespace {

// Names/index used only by these benchmarks, kept distinct from real UMD locks.
constexpr std::string_view BENCH_ROBUST_NAME = "MICROBENCH_ROBUST";
// constexpr std::string_view BENCH_FLOCK_NAME = "MICROBENCH_FLOCK";  // FlockMutex removed.
constexpr uint8_t BENCH_KMD_LOCK_INDEX = 32;

// Thread counts exercised by the contended throughput benchmark.
const std::vector<int> THREAD_COUNTS = {1, 2, 4, 8};
// Each (mechanism, thread-count) combination runs for this long; bounding by time rather than by a
// fixed iteration count keeps total runtime predictable even for slow, polling-based backends.
constexpr std::chrono::milliseconds CONTENDED_BUDGET{200};

// Returns a /dev/tenstorrent device number to benchmark KMD locks against, or nullopt if none exist.
std::optional<int> first_device() {
    std::vector<int> devices = PCIDevice::enumerate_devices();
    if (devices.empty()) {
        return std::nullopt;
    }
    return devices.front();
}

// Construct + initialize a fresh mutex of the requested kind. Each factory returns an initialized,
// ready-to-lock instance; the mutex types are move-only and movable, so returning by value is cheap.
RobustMutex make_robust() {
    RobustMutex m(BENCH_ROBUST_NAME);
    m.initialize();
    return m;
}

// FlockMutex removed; factory kept commented out so the benchmark can be re-enabled easily.
// FlockMutex make_flock() {
//     FlockMutex m(BENCH_FLOCK_NAME);
//     m.initialize();
//     return m;
// }

KmdMutex make_kmd(int device_num) {
    KmdMutex m(device_num, BENCH_KMD_LOCK_INDEX);
    m.initialize();
    return m;
}

// Measures aggregate lock()/unlock() throughput: num_threads threads, each with its own instance
// contending over the same underlying lock, each looping lock/unlock until the time budget expires.
// Returns nanoseconds per lock+unlock pair (wall-clock / total ops summed across threads).
template <typename MakeFn>
double bench_contended_threads(MakeFn make, int num_threads, std::chrono::milliseconds budget) {
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> stop{false};
    std::atomic<int64_t> total_ops{0};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            auto m = make();  // each thread owns its own instance / fd
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) {
            }
            // Check the stop flag only once per batch so the atomic load does not show up in the
            // per-op measurement. Overshooting the time budget by up to one batch is harmless: the
            // returned ns/op is elapsed_time / ops, so both numerator and denominator include the
            // overshoot and the ratio stays accurate.
            constexpr int64_t STOP_CHECK_BATCH = 1000;
            int64_t ops = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                for (int64_t i = 0; i < STOP_CHECK_BATCH; ++i) {
                    m.lock();
                    m.unlock();
                }
                ops += STOP_CHECK_BATCH;
            }
            total_ops.fetch_add(ops, std::memory_order_relaxed);
        });
    }

    // Start timing only once every thread has constructed its instance and is spinning on `go`.
    while (ready.load(std::memory_order_acquire) < num_threads) {
    }
    auto start = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(budget);
    stop.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    auto end = std::chrono::steady_clock::now();

    int64_t ops = total_ops.load(std::memory_order_relaxed);
    if (ops == 0) {
        return 0.0;
    }
    double total_ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    return total_ns / static_cast<double>(ops);
}

}  // namespace

// construct + initialize() + destroy.
TEST(MicrobenchmarkLocks, Initialize) {
    std::optional<int> device_num = first_device();

    auto bench = ankerl::nanobench::Bench().title("LockInitialize").unit("init");
    bench.name("RobustMutex").run([&] {
        auto m = make_robust();
        ankerl::nanobench::doNotOptimizeAway(&m);
    });
    // FlockMutex removed; kept commented out so the benchmark can be re-enabled easily.
    // bench.name("FlockMutex").run([&] { auto m = make_flock(); ankerl::nanobench::doNotOptimizeAway(&m); });
    if (device_num.has_value()) {
        bench.name("KmdMutex").run([&] {
            auto m = make_kmd(*device_num);
            ankerl::nanobench::doNotOptimizeAway(&m);
        });
    }
    tt::umd::test::utils::export_results(bench);
    std::cout << std::endl;
}

// Uncontended lock() + unlock() on a single thread.
TEST(MicrobenchmarkLocks, LockUnlock) {
    std::optional<int> device_num = first_device();

    auto bench = ankerl::nanobench::Bench().title("LockUnlock").unit("lock+unlock");

    auto robust = make_robust();
    bench.name("RobustMutex").run([&] {
        robust.lock();
        robust.unlock();
    });

    // FlockMutex removed; kept commented out so the benchmark can be re-enabled easily.
    // auto flock = make_flock();
    // bench.name("FlockMutex").run([&] {
    //     flock.lock();
    //     flock.unlock();
    // });

    if (device_num.has_value()) {
        auto kmd = make_kmd(*device_num);
        bench.name("KmdMutex").run([&] {
            kmd.lock();
            kmd.unlock();
        });
    }
    tt::umd::test::utils::export_results(bench);
    std::cout << std::endl;
}

// probe_lock() cost when the lock is already held by another instance (returns "busy", no acquire).
TEST(MicrobenchmarkLocks, ProbeContended) {
    std::optional<int> device_num = first_device();

    auto bench = ankerl::nanobench::Bench().title("LockProbeContended").unit("probe");

    {
        auto holder = make_robust();
        auto prober = make_robust();
        holder.lock();
        bench.name("RobustMutex").run([&] {
            auto owner = prober.probe_lock();
            ankerl::nanobench::doNotOptimizeAway(owner);
        });
        holder.unlock();
    }
    // FlockMutex removed; kept commented out so the benchmark can be re-enabled easily.
    // {
    //     auto holder = make_flock();
    //     auto prober = make_flock();
    //     holder.lock();
    //     bench.name("FlockMutex").run([&] {
    //         auto owner = prober.probe_lock();
    //         ankerl::nanobench::doNotOptimizeAway(owner);
    //     });
    //     holder.unlock();
    // }
    if (device_num.has_value()) {
        auto holder = make_kmd(*device_num);
        auto prober = make_kmd(*device_num);
        holder.lock();
        bench.name("KmdMutex").run([&] {
            auto owner = prober.probe_lock();
            ankerl::nanobench::doNotOptimizeAway(owner);
        });
        holder.unlock();
    }
    tt::umd::test::utils::export_results(bench);
    std::cout << std::endl;
}

// Column widths for the LockUnlockThreads table.
constexpr int BACKEND_COL_W = 15;
constexpr int THREAD_COL_W = 13;

// Runs the contended benchmark for `backend` across every thread count and prints one table row
// (ns/op per thread count). Templated so it accepts both plain factory functions and lambdas.
template <typename MakeFn>
void print_backend_row(const std::string& backend, MakeFn make) {
    std::cout << "| " << std::left << std::setw(BACKEND_COL_W) << backend;
    for (int num_threads : THREAD_COUNTS) {
        double ns = bench_contended_threads(make, num_threads, CONTENDED_BUDGET);
        std::cout << " | " << std::right << std::setw(THREAD_COL_W) << std::fixed << std::setprecision(2) << ns;
    }
    std::cout << " |" << std::endl;
}

// Aggregate lock()/unlock() throughput under contention from multiple threads. One row per lock
// backend, one column per thread count, reporting ns per lock+unlock pair (lower is better).
TEST(MicrobenchmarkLocks, LockUnlockThreads) {
    std::optional<int> device_num = first_device();

    std::cout << std::endl << "LockUnlockThreads - contended lock+unlock, ns/op (lower is better):" << std::endl;

    // Header row built from THREAD_COUNTS so it tracks whatever thread counts are configured.
    std::cout << "| " << std::left << std::setw(BACKEND_COL_W) << "backend";
    for (int num_threads : THREAD_COUNTS) {
        std::cout << " | " << std::right << std::setw(THREAD_COL_W) << (std::to_string(num_threads) + " threads");
    }
    std::cout << " |" << std::endl;

    // Markdown alignment separator (left-aligned name column, right-aligned numeric columns).
    std::cout << "|:" << std::string(BACKEND_COL_W, '-');
    for (size_t i = 0; i < THREAD_COUNTS.size(); ++i) {
        std::cout << "|" << std::string(THREAD_COL_W + 1, '-') << ":";
    }
    std::cout << "|" << std::endl;

    print_backend_row("RobustMutex", make_robust);
    // FlockMutex removed; kept commented out so the benchmark can be re-enabled easily.
    // print_backend_row("FlockMutex", make_flock);
    if (device_num.has_value()) {
        print_backend_row("KmdMutex", [&] { return make_kmd(*device_num); });
    }
    std::cout << std::endl;
}
