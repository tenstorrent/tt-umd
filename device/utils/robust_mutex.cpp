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
#include <sys/file.h>  // flock
#include <sys/stat.h>  // for fstat
#include <unistd.h>    // ftruncate, close

#include <stdexcept>

#include "logger.hpp"

static constexpr int ALL_RW_PERMISSION = 0666;
static constexpr std::string_view UMD_LOCK_PREFIX = "TT_UMD_LOCK.";

using namespace tt::umd;

pthread_mutex_t RobustMutex::multithread_mutex_ = PTHREAD_MUTEX_INITIALIZER;

RobustMutex::RobustMutex(std::string_view mutex_name) : mutex_name_(mutex_name) {
    initialize_pthread_mutex(mutex_name);
}

RobustMutex::~RobustMutex() noexcept { close_mutex(); }

RobustMutex::RobustMutex(RobustMutex&& other) noexcept {
    shm_fd = other.shm_fd;
    mutex_ptr = other.mutex_ptr;

    // Invalidate the other object, so the destructor doesn't try to close the same resources.
    other.shm_fd = -1;
    other.mutex_ptr = nullptr;
}

RobustMutex& RobustMutex::operator=(RobustMutex&& other) noexcept {
    if (this != &other) {
        close_mutex();  // clean up existing resources

        shm_fd = other.shm_fd;
        mutex_ptr = other.mutex_ptr;

        // Invalidate the other object, so the destructor doesn't try to close the same resources.
        other.shm_fd = -1;
        other.mutex_ptr = nullptr;
    }
    return *this;
}

void RobustMutex::initialize_pthread_mutex(std::string_view mutex_name) {
    bool created = open_shm_file(mutex_name);

    // We need a critical section here in which we test if the mutex has been initialized, and if not initialize it.
    // If we don't create a critical section for this, then two processes could race, one to initialize the mutex and
    // the other one to use it before it is initialized. flock ensures only multiprocess locking, but does not guarantee
    // multithread locking. Due to that, we need to use multithread_mutex_ which guarantees multithread locking but not
    // multiprocess locking. Note that flock is released automatically on process crash, and static multithread_mutex_
    // is not persistent, so we're safe even if the process crashes in the critical section. We use only a single
    // multithread_mutex_ for all different RobustMutex instances which can affect perf of these operations, but that is
    // fine since this is executed rarely, only on initialization and only once after booting the system. Regarding
    // flock perf, this happens only when initializing the mutex, so it is not a big deal.
    flock(shm_fd, LOCK_EX);
    pthread_mutex_lock(&multithread_mutex_);

    size_t file_size = get_file_size(shm_fd);
    // This means the file was just created but nor file nor mutex were initialized.
    bool should_initialize = (file_size == 0);

    if (should_initialize) {
        // File needs to be resized first time it is created.
        if (ftruncate(shm_fd, sizeof(pthread_mutex_t)) != 0) {
            log_fatal("ftruncate failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        }
        // Get file size again, so that the check on the next line doesn't fail.
        file_size = get_file_size(shm_fd);
    }

    // Verify file size.
    if (file_size != sizeof(pthread_mutex_t)) {
        log_fatal(
            "File size {} is not as expected {} for mutex {}. This could be due to new pthread library version, or "
            "some other external factor.",
            std::to_string(file_size),
            std::to_string(sizeof(pthread_mutex_t)),
            mutex_name_);
    }

    // We now open the mutex in the shared memory file.
    open_pthread_mutex(shm_fd);
    if (should_initialize) {
        // We need to initialize the mutex here, since it is the first time it is being used.
        initialize_pthread_mutex_first_use();
    }

    pthread_mutex_unlock(&multithread_mutex_);
    flock(shm_fd, LOCK_UN);
    // Critical section ends here.
}

bool RobustMutex::open_shm_file(std::string_view mutex_name) {
    std::string shm_file_name = std::string(UMD_LOCK_PREFIX) + std::string(mutex_name);
    bool created = true;
    // The EXCL flag will cause the call to fail if the file already exists.
    // The order of operations is important here. If we try to first open the file then create it, then a race condition
    // can occur where two processes fail to open the file and they race to create it. This way, always only one process
    // can successfully create the file.
    shm_fd = shm_open(shm_file_name.c_str(), O_RDWR | O_CREAT | O_EXCL, ALL_RW_PERMISSION);
    if (shm_fd == -1 && errno == EEXIST) {
        shm_fd = shm_open(shm_file_name.c_str(), O_RDWR, ALL_RW_PERMISSION);
        created = false;
    }
    if (shm_fd == -1) {
        log_fatal("shm_open failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
    }
    return created;
}

void RobustMutex::open_pthread_mutex(int shm_fd) {
    // Create a pthread_mutex based on the shared memory file descriptor
    void* addr = mmap(NULL, sizeof(pthread_mutex_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        log_fatal("mmap failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
    }
    mutex_ptr = (pthread_mutex_t*)addr;
}

void RobustMutex::initialize_pthread_mutex_first_use() {
    int err;
    pthread_mutexattr_t attr;
    err = pthread_mutexattr_init(&attr);
    if (err != 0) {
        log_fatal("pthread_mutexattr_init failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    }
    // This marks the mutex as being shared across processes. Not sure if this is necessary given that it resides in
    // shared memory.
    err = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (err != 0) {
        log_fatal("pthread_mutexattr_setpshared failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    }
    // This marks the mutex as robust. This will have the effect in the case of process crashing, another process
    // waiting on the mutex will get the signal and will get the flag that the previous owner of mutex died, so it can
    // recover the mutex state.
    err = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (err != 0) {
        log_fatal("pthread_mutexattr_setrobust failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    }
    err = pthread_mutex_init(mutex_ptr, &attr);
    if (err != 0) {
        log_fatal("pthread_mutex_init failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    }
}

size_t RobustMutex::get_file_size(int fd) {
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        log_fatal("fstat failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
    }
    return sb.st_size;
}

void RobustMutex::close_mutex() noexcept {
    if (mutex_ptr) {
        // Unmap the shared memory backed pthread_mutex object.
        if (munmap((void*)mutex_ptr, sizeof(pthread_mutex_t)) != 0) {
            log_warning(LogSiliconDriver, "munmap failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        }
        mutex_ptr = nullptr;
    }
    if (shm_fd != -1) {
        // Close shared memory file.
        if (close(shm_fd) != 0) {
            log_warning(LogSiliconDriver, "close failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        }
        shm_fd = -1;
    }
}

void RobustMutex::unlock() {
    int err = pthread_mutex_unlock(mutex_ptr);
    if (err != 0) {
        log_fatal("pthread_mutex_unlock failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    }
}

void RobustMutex::lock() {
    int lock_res = pthread_mutex_lock(mutex_ptr);

    if (lock_res == EOWNERDEAD) {
        // Some other process crashed before unlocking the mutex.
        // We can recover the mutex state.
        int err = pthread_mutex_consistent(mutex_ptr);
        if (err != 0) {
            log_fatal("pthread_mutex_consistent failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
        }
    } else if (lock_res != 0) {
        log_fatal("pthread_mutex_lock failed for mutex {} errno: {}", mutex_name_, std::to_string(lock_res));
    }
}
