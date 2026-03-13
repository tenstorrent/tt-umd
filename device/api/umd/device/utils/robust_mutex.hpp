// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pthread.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace tt::umd {

// RobustMutex is a class that provides a robust locking mechanism using POSIX shared memory mutexes.
// Robust means that it survives process crashes and can be used across multiple processes.
// It meets BasicLockable requirement, so it can be used with C++ standard library RAII lock classes like lock_guard and
// unique_lock.
// Note that the implementation relies on the client not deleting underlying /dev/shm files.
// Also, if the pthread implementation changes, we can enter some weird states if one process is holding the old mutex,
// and the new one tries to initialize it with the new pthread.
class RobustMutex {
public:
    // Prefix used for shared memory files backing each mutex.
    static constexpr std::string_view SHM_FILE_PREFIX = "TT_UMD_LOCK.";

    RobustMutex(std::string_view mutex_name);
    ~RobustMutex() noexcept;

    // Does everything related to initializing the mutex, even on first time creation.
    // The initialization can fail and throw an exception. In case of failure we still want to clean up the resources.
    // For easier handling of such case, the destructor cleans up the resources if they were taken. Having this code out
    // of constructor will guarantee that the destructor is called in case of exception.
    void initialize();

    // Move constructor and assignment are defined so that this class can be used with c++ stl containers, like
    // unordered_map.
    RobustMutex(RobustMutex&& other) noexcept;
    RobustMutex& operator=(RobustMutex&& other) noexcept;

    // Copy constructor and assignment are not allowed.
    RobustMutex(const RobustMutex&) = delete;
    RobustMutex& operator=(const RobustMutex&) = delete;

    // Locks the mutex, blocking indefinitely.  Uses a 1-second timed attempt first so that a
    // warning can be emitted when the lock is contended before blocking without a timeout.
    void lock();

    // Unlocks the mutex.
    void unlock();

    // Attempts to acquire the lock and returns immediately if timeout is zero (default), or waits
    // up to `timeout` seconds before giving up.
    // Returns std::nullopt if the lock was acquired successfully.
    // Returns {owner_pid, owner_tid} if the lock is held by another thread/process (EBUSY/ETIMEDOUT).
    // On EOWNERDEAD the dead owner's lock is recovered, the mutex is acquired, and nullopt is returned.
    std::optional<std::pair<pid_t, pid_t>> try_lock(std::chrono::seconds timeout = std::chrono::seconds(0));

private:
    // A wrapper which holds the flag for whether the mutex has been initialized or not,
    // and tracks the PID of the current lock owner.
    struct pthread_mutex_wrapper {
        pthread_mutex_t mutex;
        uint64_t initialized;
        pid_t owner_tid;  // TID of the thread holding the lock, 0 if no owner
        pid_t owner_pid;  // PID of the thread holding the lock, 0 if no owner
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

    // Sets owner TID/PID to the calling thread and annotates for TSAN.
    void record_acquisition();

    // Used for critical section needed during initialization.
    static pthread_mutex_t multithread_mutex_;

    int shm_fd_ = -1;
    pthread_mutex_wrapper* mutex_wrapper_ptr_ = nullptr;
    std::string mutex_name_;
};

}  // namespace tt::umd
