/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <pthread.h>

#include <cstdint>
#include <memory>
#include <string>

#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {

// RobustSystemMutex provides a robust locking mechanism using POSIX shared memory mutexes.
// Robust means that it survives process crashes and can be used across multiple processes.
// Note that the implementation relies on the client not deleting underlying /dev/shm files.
// Also, if the pthread implementation changes, we can enter some weird states if one process is holding the old mutex,
// and the new one tries to initialize it with the new pthread.
class RobustSystemMutex : public RobustMutex {
public:
    RobustSystemMutex(std::string_view mutex_name);
    ~RobustSystemMutex() noexcept;

    // Does everything related to initializing the mutex, even on first time creation.
    // The initialization can fail and throw an exception. In case of failure we still want to clean up the resources.
    // For easier handling of such case, the destructor cleans up the resources if they were taken. Having this code out
    // of constructor will guarantee that the destructor is called in case of exception.
    void initialize() override;

    // Move constructor and assignment are defined so that this class can be used with c++ stl containers, like
    // unordered_map.
    RobustSystemMutex(RobustSystemMutex&& other) noexcept;
    RobustSystemMutex& operator=(RobustSystemMutex&& other) noexcept;

    // Copy constructor and assignment are not allowed.
    RobustSystemMutex(const RobustSystemMutex&) = delete;
    RobustSystemMutex& operator=(const RobustSystemMutex&) = delete;

    // Locks and unlocks the mutex.
    void unlock() override;
    void lock() override;

private:
    // A wrapper which holds the flag for whether the mutex has been initialized or not.
    struct pthread_mutex_wrapper {
        pthread_mutex_t mutex;
        uint64_t initialized;
    };

    // Closes the mutex, doesn't remove the backing mutex file.
    void close_mutex() noexcept;

    // Opens the shared memory file and creates it if it doesn't exist.
    void open_shm_file();

    // Resizes the shared memory file to the size of the mutex wrapper.
    // Returns whether the file was resized or not.
    bool resize_shm_file();

    // Opens the mutex in the shared memory file.
    void open_pthread_mutex();

    // Gets the size of the file.
    size_t get_file_size(int fd);

    // Performs initialization for the first time pthread mutex use.
    void initialize_pthread_mutex_first_use();

    // Used for critical section needed during initialization.
    static pthread_mutex_t multithread_mutex_;

    int shm_fd_ = -1;
    pthread_mutex_wrapper* mutex_wrapper_ptr_ = nullptr;
    std::string mutex_name_;
};

}  // namespace tt::umd
