/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/utils/robust_mutex.h"

#include <sys/mman.h>  // shm_open, shm_unlink, mmap, munmap,
                       // PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED
#include <errno.h>     // errno, ENOENT
#include <fcntl.h>     // O_RDWR, O_CREATE
#include <pthread.h>   // pthread_mutexattr_init, pthread_mutexattr_setpshared, pthread_mutex_t
#include <sys/stat.h>  // for fstat
#include <unistd.h>    // ftruncate, close

#include <stdexcept>

static constexpr int ALL_RW_PERMISSION = 0666;
static constexpr std::string_view UMD_LOCK_PREFIX = "umdlock.";

using namespace tt::umd;

RobustMutex::RobustMutex(std::string mutex_name) : mutex_name_(mutex_name) { initialize_pthread_mutex(mutex_name); }

RobustMutex::~RobustMutex() { close_mutex(); }

void RobustMutex::initialize_pthread_mutex(std::string mutex_name) {
    bool created = open_shm_file(mutex_name);
    // There is a slight chance of race condition here, where first process creates the file, and second process opens
    // it. Then the second process "surpasses" the first process and tries using the mutex before it is initialized.
    if (created) {
        // File needs to be resized first time it is created.
        if (ftruncate(shm_fd, sizeof(pthread_mutex_t)) != 0) {
            throw std::runtime_error(
                "ftruncate failed for mutex " + mutex_name + " errnofor mutex " + mutex_name_ +
                " errno: " + std::to_string(errno));
        }
    }
    // Verify file size.
    size_t file_size = get_file_size(shm_fd);
    if (file_size != sizeof(pthread_mutex_t)) {
        throw std::runtime_error(
            "File size " + std::to_string(file_size) + " is not as expected " +
            std::to_string(sizeof(pthread_mutex_t)) +
            ", this could be due to race condition or some external factor. Mutex: " + mutex_name);
    }

    open_pthread_mutex(shm_fd);
    if (created) {
        initialize_pthread_mutex_first_use();
    }
}

bool RobustMutex::open_shm_file(std::string mutex_name) {
    std::string shm_file_name = std::string(UMD_LOCK_PREFIX) + mutex_name;
    bool created = false;
    shm_fd = shm_open(shm_file_name.c_str(), O_RDWR, ALL_RW_PERMISSION);
    // This indicates error opening the file.
    // If the file doesn't exist, create it instead.
    if (shm_fd == -1 && errno == ENOENT) {
        // There is a slight chance of race condition here, since two processes can enter this same codepath, and then
        // both can try creating the file.
        shm_fd = shm_open(shm_file_name.c_str(), O_RDWR | O_CREAT, ALL_RW_PERMISSION);
        created = true;
    }
    if (shm_fd == -1) {
        throw std::runtime_error("shm_open failed for mutex " + mutex_name_ + " errno: " + std::to_string(errno));
    }
    return created;
}

void RobustMutex::open_pthread_mutex(int shm_fd) {
    // Create a pthread_mutex based on the shared memory file descriptor
    void *addr = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        throw std::runtime_error("mmap failed for mutex " + mutex_name_ + " errno: " + std::to_string(errno));
    }
    mutex_ptr = (pthread_mutex_t *)addr;
}

void RobustMutex::initialize_pthread_mutex_first_use() {
    int err;
    pthread_mutexattr_t attr;
    err = pthread_mutexattr_init(&attr);
    if (err != 0) {
        throw std::runtime_error(
            "pthread_mutexattr_init failed for mutex " + mutex_name_ + " errno: " + std::to_string(err));
    }
    // This marks the mutex as being shared across processes. Not sure if this is necessary given that it resides in
    // shared memory.
    err = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (err != 0) {
        throw std::runtime_error(
            "pthread_mutexattr_setpshared failed for mutex " + mutex_name_ + " errno: " + std::to_string(err));
    }
    // This marks the mutex as robust. This will have the effect in the case of process crashing, another process
    // waiting on the mutex will get the signal and will get the flag that the previous owner of mutex died, so it can
    // recover the mutex state.
    err = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (err != 0) {
        throw std::runtime_error(
            "pthread_mutexattr_setrobust failed for mutex " + mutex_name_ + " errno: " + std::to_string(err));
    }
    err = pthread_mutex_init(mutex_ptr, &attr);
    if (err != 0) {
        throw std::runtime_error(
            "pthread_mutex_init failed for mutex " + mutex_name_ + " errno: " + std::to_string(err));
    }
}

size_t RobustMutex::get_file_size(int fd) {
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        throw std::runtime_error("fstat failed for mutex " + mutex_name_ + " errno: " + std::to_string(errno));
    }
    return sb.st_size;
}

void RobustMutex::close_mutex() {
    // Unmap the shared memory backed pthread_mutex object.
    if (munmap((void *)mutex_ptr, sizeof(pthread_mutex_t)) != 0) {
        throw std::runtime_error("munmap failed for mutex " + mutex_name_ + " errno: " + std::to_string(errno));
    }
    mutex_ptr = nullptr;
    // Close shared memory file.
    if (close(shm_fd) != 0) {
        throw std::runtime_error("close failed for mutex " + mutex_name_ + " errno: " + std::to_string(errno));
    }
    shm_fd = -1;
}

void RobustMutex::unlock_mutex() {
    int err = pthread_mutex_unlock(mutex_ptr);
    if (err != 0) {
        throw std::runtime_error(
            "pthread_mutex_unlock failed for mutex " + mutex_name_ + " errno: " + std::to_string(err));
    }
}

void RobustMutex::lock_mutex() {
    int lock_res = pthread_mutex_lock(mutex_ptr);

    if (lock_res == EOWNERDEAD) {
        // Some other process crashed before unlocking the mutex.
        // We can recover the mutex state.
        int err = pthread_mutex_consistent(mutex_ptr);
        if (err != 0) {
            throw std::runtime_error(
                "pthread_mutex_consistent failed for mutex " + mutex_name_ + " errno: " + std::to_string(err));
        }
    } else if (lock_res != 0) {
        throw std::runtime_error(
            "pthread_mutex_lock failed for mutex " + mutex_name_ + " errno: " + std::to_string(lock_res));
    }
}

RAIIMutex::RAIIMutex(RobustMutex *mutex) : mutex_ptr(mutex) { mutex_ptr->lock_mutex(); }

RAIIMutex::~RAIIMutex() { mutex_ptr->unlock_mutex(); }
