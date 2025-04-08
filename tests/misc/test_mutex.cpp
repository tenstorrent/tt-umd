/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include "umd/device/utils/robust_mutex.h"

using namespace tt::umd;

TEST(RobustMutexTest, RobustMutex) {
    RobustMutex mutex("test_mutex");
    mutex.initialize();
    mutex.lock();
    std::cout << "locked mutex" << std::endl;

    int a = 1;
    for (int i = 0; i < 1000000000; i++) {
        a = a * i + a;
        std::cin >> a;
    }
}

// TEST(RobustMutexTest, Benchmark) {

//     // Create mutex once so it is initialized, so that this is hot.
//     {
//         RobustMutex mutex("test_mutex");
//         mutex.initialize();
//     }
//     // Start measuring time
//     auto start = std::chrono::high_resolution_clock::now();
//     // Create mutex multiple times to benchmark the creation time.
//     for (int i = 0; i < 10000000; i++) {
//     // for (int i = 0; i < 10000; i ++) {
//         RobustMutex mutex("test_mutex");
//         mutex.initialize();
//     }
//     // Stop measuring time
//     auto end = std::chrono::high_resolution_clock::now();

//     // report the time taken for a single call
//     auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
//     std::cout << "Time taken for 10000000 mutex initializations: " << duration.count() << " nanoseconds, so single
//     one is around " << duration.count()/1000000 << std::endl;

//     // Now measure time for lock unlock
//     {
//         RobustMutex mutex("test_mutex");
//         mutex.initialize();
//         auto start = std::chrono::high_resolution_clock::now();
//         for (int i = 0; i < 10000000; i++) {
//             mutex.lock();
//             mutex.unlock();
//         }
//         auto end = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
//         std::cout << "Time taken for 10000000 lock/unlock: " << duration.count() << " nanoseconds, so single one is
//         around " << duration.count()/1000000 << std::endl;
//     }

//     // Now measure time using unique_lock
//     {
//         RobustMutex mutex("test_mutex");
//         mutex.initialize();
//         auto start = std::chrono::high_resolution_clock::now();
//         for (int i = 0; i < 10000000; i++) {
//             std::unique_lock<RobustMutex> lock(mutex);
//         }
//         auto end = std::chrono::high_resolution_clock::now();
//         auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
//         std::cout << "Time taken for 10000000 unique_lock/unlock: " << duration.count() << " nanoseconds, so single
//         one is around " << duration.count()/1000000 << std::endl;
//     }

// }
