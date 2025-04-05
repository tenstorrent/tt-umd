/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>

namespace tt::umd {

// RobustMutex is a class that provides a robust locking mechanism using POSIX shared memory mutexes.
// Robust means that it survives process crashes and can be used across multiple processes.
// It meets BasicLockable requirement, so it can be used with C++ standard library RAII lock classes like lock_guard and
// unique_lock.
class RobustMutex {
public:
    RobustMutex(std::string_view mutex_name);
    ~RobustMutex() noexcept;

    // Move constructor and assignment are defined so that this class can be used with c++ stl containers, like
    // unordered_map.
    RobustMutex(RobustMutex&& other) noexcept;
    RobustMutex& operator=(RobustMutex&& other) noexcept;

    // Copy constructor and assignment are not allowed.
    RobustMutex(const RobustMutex&) = delete;
    RobustMutex& operator=(const RobustMutex&) = delete;

    // Locks and unlocks the mutex.
    void unlock();
    void lock();

private:
    static pthread_mutex_t multithread_mutex_;

    // Does everything related to initializing the mutex, even on first time creation.
    void initialize_pthread_mutex(std::string_view mutex_name);

    // Closes the mutex, doesn't remove the backing mutex file.
    void close_mutex() noexcept;

    // Opens the shared memory file and creates it if it doesn't exist.
    bool open_shm_file(std::string_view mutex_name);

    // Opens the mutex in the shared memory file.
    void open_pthread_mutex(int shm_fd);

    // Gets the size of the file.
    size_t get_file_size(int fd);

    // Performs initialization for the first time pthread mutex use.
    void initialize_pthread_mutex_first_use();

    int shm_fd = -1;
    pthread_mutex_t* mutex_ptr = nullptr;
    std::string mutex_name_;
};

}  // namespace tt::umd
