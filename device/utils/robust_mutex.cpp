// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/robust_mutex.hpp"

#include <sys/mman.h>  // shm_open, shm_unlink, mmap, munmap,

#include "assert.hpp"
// PROT_READ, PROT_WRITE, MAP_SHARED, MAP_FAILED.
#include <errno.h>     // errno, ENOENT
#include <fcntl.h>     // O_RDWR, O_CREATE
#include <pthread.h>   // pthread_mutexattr_init, pthread_mutexattr_setpshared, pthread_mutex_t
#include <sys/file.h>  // flock
#include <sys/stat.h>  // for fstat
#include <time.h>      // clock_gettime, timespec
#include <unistd.h>    // ftruncate, close, gettid

#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

// TSAN (ThreadSanitizer) annotations for cross-process mutex synchronization.
// These are only available when building with TSAN enabled.

// Unify TSAN detection: Ensure __SANITIZE_THREAD__ is defined for Clang.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) && !defined(__SANITIZE_THREAD__)
#define __SANITIZE_THREAD__ 1
#endif
#endif

// Declare TSAN hooks once.
#ifdef __SANITIZE_THREAD__
extern "C" {
void __tsan_acquire(void* addr);
void __tsan_release(void* addr);
}
#endif

namespace tt::umd {

static constexpr int ALL_RW_PERMISSION = 0666;
static constexpr std::string_view UMD_LOCK_PREFIX = "TT_UMD_LOCK.";
// Any value which is unlikely to be found at random in the memory.
static constexpr uint64_t INITIALIZED_FLAG = 0x5454554d444d5458;  // TTUMDMTX

// A small helper class which will ensure that the critical section is released in a RAII manner.
// flock ensures only multiprocess locking, but does not guarantee
// multithread locking. Due to that, we need to use multithread_mutex_ which guarantees multithread locking but not
// multiprocess locking. Note that flock is released automatically on process crash, and static multithread_mutex_
// is not persistent, so we're safe even if the process crashes in the critical section.
//
// One might wonder, if this is already a guaranteed critical section, why do we need to go through all the pain
// to setup pthread in shm? Quick benchmark gave this results averaged over 1 000 000 iterations:
//   RobustMutex constructor + initialization + destructor: 40752 ns
//   RobustMutex lock + unlock: 654 ns.
class CriticalSectionScopeGuard {
public:
    CriticalSectionScopeGuard(int fd, pthread_mutex_t* pthread_mutex, std::string_view mutex_name) :
        fd_(fd), pthread_mutex_(pthread_mutex), mutex_name_(mutex_name) {
        TT_ASSERT(flock(fd_, LOCK_EX) == 0, "flock failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        int err = pthread_mutex_lock(pthread_mutex_);
        if (err != 0) {
            // Try to unlock the flock without handling further exceptions.
            flock(fd_, LOCK_UN);
            TT_ASSERT(false, "pthread_mutex_lock failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
        }
    }

    ~CriticalSectionScopeGuard() noexcept {
        // Use best effort to unlock the mutex and the flock and report warnings if something fails.
        int err = pthread_mutex_unlock(pthread_mutex_);
        if (err != 0) {
            // This is on the destructor path, so we don't want to throw an exception.
            log_warning(
                tt::LogUMD, "pthread_mutex_unlock failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
        }
        if (flock(fd_, LOCK_UN) != 0) {
            // This is on the destructor path, so we don't want to throw an exception.
            log_warning(tt::LogUMD, "flock failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        }
    }

private:
    int fd_;
    pthread_mutex_t* pthread_mutex_;
    std::string mutex_name_;
};

pthread_mutex_t RobustMutex::multithread_mutex_ = PTHREAD_MUTEX_INITIALIZER;

#ifdef __SANITIZE_THREAD__
// Static storage for TSAN mutex identifiers.
// TSAN needs stable, valid memory addresses to track mutex synchronization.
// For cross-process mutexes where each process maps shared memory to different
// virtual addresses, we maintain a process-local map from mutex name to a
// stable address that TSAN can use for tracking happens-before relationships.
static std::unordered_map<std::string, char> tsan_mutex_id_storage;
static std::mutex tsan_storage_mutex;

void* get_tsan_mutex_id(std::string mutex_name) {
    // TSAN needs a stable, valid memory address to track mutex synchronization.
    // For cross-process mutexes, each process maps shared memory to different
    // virtual addresses. To ensure TSAN understands that mutexes with the same
    // name are the same logical mutex across all threads/processes, we maintain
    // a process-local map from mutex name to a stable address.
    //
    // This function assumes initialize() has already been called, which registers
    // the mutex in tsan_mutex_id_storage. Since we only read from the map here
    // (after initialization), no locking is needed.
    auto it = tsan_mutex_id_storage.find(mutex_name);
    TT_ASSERT(
        it != tsan_mutex_id_storage.end(),
        "TSAN mutex ID not found for mutex '{}'. initialize() must be called before lock/unlock.",
        mutex_name);
    return static_cast<void*>(&it->second);
}
#endif

static void tsan_annotate_mutex_init(const std::string& name) {
#ifdef __SANITIZE_THREAD__
    // Register this mutex name in the TSAN tracking storage.
    // This ensures all threads using the same mutex name will use the same
    // address for TSAN synchronization tracking.
    std::lock_guard<std::mutex> lock(tsan_storage_mutex);
    if (tsan_mutex_id_storage.find(mutex_name) == tsan_mutex_id_storage.end()) {
        tsan_mutex_id_storage[mutex_name] = 0;
    }
#endif
}

static void tsan_annotate_mutex_acquire(const std::string& name) {
#ifdef __SANITIZE_THREAD__
    // Inform TSAN that we've acquired the mutex, establishing a happens-before relationship.
    // This must be called AFTER the actual lock acquisition so TSAN sees that we now have
    // the synchronization point established by the previous owner's __tsan_release.
    __tsan_acquire(get_tsan_mutex_id(name));
#endif
}

static void tsan_annotate_mutex_release(const std::string& name) {
#ifdef __SANITIZE_THREAD__
    // Inform TSAN that we're releasing the mutex, establishing a happens-before relationship.
    // This must be called BEFORE the actual unlock so TSAN sees the release before other
    // threads/processes can acquire the lock.
    __tsan_release(get_tsan_mutex_id(name));
#endif
}

RobustMutex::RobustMutex(std::string_view mutex_name) : mutex_name_(mutex_name) {}

RobustMutex::~RobustMutex() noexcept { close_mutex(); }

RobustMutex::RobustMutex(RobustMutex&& other) noexcept {
    shm_fd_ = other.shm_fd_;
    mutex_wrapper_ptr_ = other.mutex_wrapper_ptr_;
    mutex_name_ = other.mutex_name_;

    // Invalidate the other object, so the destructor doesn't try to close the same resources.
    other.shm_fd_ = -1;
    other.mutex_wrapper_ptr_ = nullptr;
    other.mutex_name_ = "";
}

RobustMutex& RobustMutex::operator=(RobustMutex&& other) noexcept {
    if (this != &other) {
        close_mutex();  // clean up existing resources

        shm_fd_ = other.shm_fd_;
        mutex_wrapper_ptr_ = other.mutex_wrapper_ptr_;
        mutex_name_ = other.mutex_name_;

        // Invalidate the other object, so the destructor doesn't try to close the same resources.
        other.shm_fd_ = -1;
        other.mutex_wrapper_ptr_ = nullptr;
        other.mutex_name_ = "";
    }
    return *this;
}

void RobustMutex::initialize() {
    open_shm_file();

    // We need a critical section here in which we test if the mutex has been initialized, and if not initialize it.
    // If we don't create a critical section for this, then two processes could race, one to initialize the mutex and
    // the other one to use it before it is initialized. We use only a single
    // multithread_mutex_ for all different RobustMutex instances which can affect perf of these operations, but that is
    // fine since this is executed rarely, only on initialization and only once after booting the system. Regarding
    // flock perf, this happens only when initializing the mutex, so it is not a big deal.
    // The critical_section object will get destroyed at the end of this block or when an exception is thrown, so the
    // critical section will be released automatically.
    {
        CriticalSectionScopeGuard critical_section(shm_fd_, &multithread_mutex_, mutex_name_);

        // Resize file if needed.
        bool file_was_resized = resize_shm_file();

        // We now open the mutex in the shared memory file.
        open_pthread_mutex();

        // Report warning in case:
        //  - File was not resized, but the initialized flag is wrong.
        //  - File was resized, but the initialized flag is correct (this is a bit unexpected, but theoretically
        //  possible).
        if (mutex_wrapper_ptr_->initialized != INITIALIZED_FLAG && !file_was_resized) {
            log_warning(
                tt::LogUMD,
                "The file was already of correct size, but the initialized flag is wrong. This could "
                "be due to previously failed initialization, or some other external factor. Mutex name: {}",
                mutex_name_);
        }
        if (mutex_wrapper_ptr_->initialized == INITIALIZED_FLAG && file_was_resized) {
            log_warning(
                tt::LogUMD,
                "The file was resized, but the initialized flag is correct. This is an unexpected "
                "case, the mutex might fail. Mutex name: {}",
                mutex_name_);
        }

        // Initialize the mutex if it wasn't properly initialized before.
        if (mutex_wrapper_ptr_->initialized != INITIALIZED_FLAG) {
            // We need to initialize the mutex here, since it is the first time it is being used.
            initialize_pthread_mutex_first_use();
        }
    }  // CriticalSectionScopeGuard destructor is called here, releasing the flock and mutex

    // Close the file descriptor after mapping is complete.
    // The mapped memory will remain valid even after closing the fd.
    // This helps avoid hitting file descriptor limits on systems with many chips.
    if (close(shm_fd_) != 0) {
        log_warning(tt::LogUMD, "close failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
    }
    shm_fd_ = -1;

    tsan_annotate_mutex_init(mutex_name_);
}

void RobustMutex::open_shm_file() {
    std::string shm_file_name = std::string(UMD_LOCK_PREFIX) + std::string(mutex_name_);

    // Store old mask and clear processes umask.
    // This will have an effect that the created files which back up named mutexes will have all permissions. This is
    // important to avoid permission issues between processes.
    auto old_umask = umask(0);

    // The EXCL flag will cause the call to fail if the file already exists.
    // The order of operations is important here. If we try to first open the file then create it, then a race condition
    // can occur where two processes fail to open the file and they race to create it. This way, always only one process
    // can successfully create the file.
    shm_fd_ = shm_open(shm_file_name.c_str(), O_RDWR | O_CREAT | O_EXCL, ALL_RW_PERMISSION);
    if (shm_fd_ == -1 && errno == EEXIST) {
        shm_fd_ = shm_open(shm_file_name.c_str(), O_RDWR, ALL_RW_PERMISSION);
    }

    // Restore old mask.
    umask(old_umask);

    TT_ASSERT(shm_fd_ != -1, "shm_open failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
}

bool RobustMutex::resize_shm_file() {
    size_t file_size = get_file_size(shm_fd_);
    size_t target_file_size = sizeof(pthread_mutex_wrapper);
    bool file_was_truncated = false;

    // Report warning if the file size is not as expected, but continue with the initialization.
    if (file_size != 0 && file_size != target_file_size) {
        log_warning(
            tt::LogUMD,
            "File size {} is not as expected {} for mutex {}. This could be due to new pthread library version, or "
            "some other external factor.",
            std::to_string(file_size),
            std::to_string(target_file_size),
            mutex_name_);
    }
    // If file size is different from the needed size, we should resize it to proper size.
    // This includes the case when file_size was just created and its size iz 0.
    if (file_size != target_file_size) {
        TT_ASSERT(
            ftruncate(shm_fd_, target_file_size) == 0,
            "ftruncate failed for mutex {} errno: {}",
            mutex_name_,
            std::to_string(errno));
        file_was_truncated = true;

        // Verify file size again. This time throw an exception.
        file_size = get_file_size(shm_fd_);
        TT_ASSERT(
            file_size == target_file_size,
            "File size {} is not as expected {} for mutex {}. This could be due to new pthread library version, or "
            "some other external factor.",
            std::to_string(file_size),
            std::to_string(target_file_size),
            mutex_name_);
    }

    return file_was_truncated;
}

void RobustMutex::open_pthread_mutex() {
    // Create a pthread_mutex based on the shared memory file descriptor.
    void* addr = mmap(NULL, sizeof(pthread_mutex_wrapper), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    TT_ASSERT(addr != MAP_FAILED, "mmap failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
    mutex_wrapper_ptr_ = static_cast<pthread_mutex_wrapper*>(addr);
}

void RobustMutex::initialize_pthread_mutex_first_use() {
    int err;
    pthread_mutexattr_t attr;
    err = pthread_mutexattr_init(&attr);
    TT_ASSERT(err == 0, "pthread_mutexattr_init failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    // This marks the mutex as being shared across processes. Not sure if this is necessary given that it resides in
    // shared memory.
    err = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    TT_ASSERT(err == 0, "pthread_mutexattr_setpshared failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    // This marks the mutex as robust. This will have the effect in the case of process crashing, another process
    // waiting on the mutex will get the signal and will get the flag that the previous owner of mutex died, so it can
    // recover the mutex state.
    err = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    TT_ASSERT(err == 0, "pthread_mutexattr_setrobust failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    err = pthread_mutex_init(&(mutex_wrapper_ptr_->mutex), &attr);
    TT_ASSERT(err == 0, "pthread_mutex_init failed for mutex {} errno: {}", mutex_name_, std::to_string(err));
    // When we open an existing pthread in the future, there is no other way to check if it was initialized or not, so
    // we need to set this flag.
    mutex_wrapper_ptr_->initialized = INITIALIZED_FLAG;
    // Initialize owner TID and PID to 0 (no owner).
    mutex_wrapper_ptr_->owner_tid = 0;
    mutex_wrapper_ptr_->owner_pid = 0;
}

size_t RobustMutex::get_file_size(int fd) {
    struct stat sb;
    TT_ASSERT(fstat(fd, &sb) == 0, "fstat failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
    return sb.st_size;
}

void RobustMutex::close_mutex() noexcept {
    if (mutex_wrapper_ptr_ != nullptr) {
        // Unmap the shared memory backed pthread_mutex object.
        if (munmap((void*)mutex_wrapper_ptr_, sizeof(pthread_mutex_wrapper)) != 0) {
            // This is on the destructor path, so we don't want to throw an exception.
            log_warning(tt::LogUMD, "munmap failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        }
        mutex_wrapper_ptr_ = nullptr;
    }
    if (shm_fd_ != -1) {
        // Close shared memory file descriptor if it's still open.
        // Note: In normal operation, this fd is closed at the end of initialize(),
        // but we still handle cleanup here for safety (e.g., if initialization failed partway through).
        if (close(shm_fd_) != 0) {
            // This is on the destructor path, so we don't want to throw an exception.
            log_warning(tt::LogUMD, "close failed for mutex {} errno: {}", mutex_name_, std::to_string(errno));
        }
        shm_fd_ = -1;
    }
}

void RobustMutex::unlock() {
    tsan_annotate_mutex_release(mutex_name_);

    // Clear the owner TID and PID before unlocking.
    mutex_wrapper_ptr_->owner_tid = 0;
    mutex_wrapper_ptr_->owner_pid = 0;
    int err = pthread_mutex_unlock(&(mutex_wrapper_ptr_->mutex));
    if (err != 0) {
        TT_THROW(fmt::format("pthread_mutex_unlock failed for mutex {} errno: {}", mutex_name_, std::to_string(err)));
    }
}

void RobustMutex::lock() {
    // Try to acquire the lock with a 1-second timeout first.
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 1;  // 1 second timeout

    int lock_res = pthread_mutex_timedlock(&(mutex_wrapper_ptr_->mutex), &timeout);

    // First lock attempt is there so that we can log something to the user in case they aren't able to acquire the lock
    // immediately. Note that since the call inside this loop is blocking, this code will loop at most once, but it is
    // still more concise than to write duplicate code for handling the first lock try attempt and the second one.
    while (lock_res != 0) {
        if (lock_res == EOWNERDEAD) {
            // Process crashed before unlocking the mutex. Recover it.
            int err = pthread_mutex_consistent(&(mutex_wrapper_ptr_->mutex));
            if (err != 0) {
                TT_THROW(fmt::format(
                    "pthread_mutex_consistent failed for mutex {} errno: {}", mutex_name_, std::to_string(err)));
            }
            // Break out of the loop as we can now successfully lock.
            lock_res = 0;
        } else if (lock_res == ETIMEDOUT) {
            // Timeout occurred - log a message about waiting.
            // Note that we can enter here only as a result of timedlock version.
            log_warning(
                LogUMD,
                "Waiting for lock '{}' which is currently held by thread TID: {}, PID: {}",
                mutex_name_,
                mutex_wrapper_ptr_->owner_tid,
                mutex_wrapper_ptr_->owner_pid);

            // Now block until we get the lock.
            lock_res = pthread_mutex_lock(&(mutex_wrapper_ptr_->mutex));
        } else {
            // Lock operation failed, either after first or second attempt.
            TT_THROW(
                fmt::format("pthread_mutex_lock failed for mutex {} errno: {}", mutex_name_, std::to_string(lock_res)));
        }
    }

    // lock_res is 0, so this is a success case.
    mutex_wrapper_ptr_->owner_tid = gettid();
    mutex_wrapper_ptr_->owner_pid = getpid();

    tsan_annotate_mutex_acquire(mutex_name_);
}

}  // namespace tt::umd
