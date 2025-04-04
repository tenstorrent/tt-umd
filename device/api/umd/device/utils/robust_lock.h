/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>

namespace tt::umd {

// RobustLock is a class that provides a robust locking mechanism using POSIX shared memory mutexes.
// Robust means that it survives process crashes and can be used across multiple processes.
// This class implements RAII idiom, so on creation the mutex is locked, and on destruction it is unlocked.
// Suggested way to use it is to create a unique_ptr to it, and let it go out of scope when done.
class RobustLock {
public:
    RobustLock(std::string mutex_name);
    ~RobustLock();

private:
    // Does everything related to initializing the mutex, even on first time creation.
    void initialize_pthread_mutex(std::string mutex_name);
    // Closes the mutex, doesn't remove the backing mutex file.
    void close_mutex();

    // Locks and unlocks the mutex.
    void unlock_mutex();
    void lock_mutex();

    // Opens the shared memory file and creates it if it doesn't exist.
    bool open_shm_file(std::string mutex_name);
    // Opens the mutex in the shared memory file.
    void open_pthread_mutex(int shm_fd);
    // Gets the size of the file.
    size_t get_file_size(int fd);
    // Performs initialization for the first time pthread mutex use.
    void initialize_pthread_mutex_first_use();

    int shm_fd = -1;
    pthread_mutex_t *mutex_ptr = nullptr;
};

}  // namespace tt::umd
