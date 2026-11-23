// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/utils/lock_manager.hpp"

#include <sys/syscall.h>  // SYS_gettid
#include <unistd.h>       // getpid, syscall

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

#include "tt-umd-utils/robust_mutex.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/kmd_mutex.hpp"

namespace tt::umd {

namespace {

// Name of each mutex type, the stem of every lock name made from it.
const std::unordered_map<MutexType, std::string> MUTEX_TYPE_TO_STRING = {
    {MutexType::ARC_MSG, "ARC_MSG"},
    {MutexType::REMOTE_ARC_MSG, "REMOTE_ARC_MSG"},
    {MutexType::NON_MMIO, "NON_MMIO"},
    {MutexType::MEM_BARRIER, "MEM_BARRIER"},
    {MutexType::CREATE_ETH_MAP, "CREATE_ETH_MAP"},
    {MutexType::CHIP_IN_USE, "CHIP_IN_USE"},
    {MutexType::PCIE_DMA, "PCIE_DMA"},
};

std::string get_mutex_name(MutexType mutex_type, int device_id, IODeviceType device_type) {
    return MUTEX_TYPE_TO_STRING.at(mutex_type) + "_" + std::to_string(device_id) + "_" +
           DeviceTypeToString.at(device_type);
}

// KMD resource lock index used for each chip specific mutex type. KMD suggests indices 0..15 for ERISC cores (see
// tt_kmd_lib.h), so UMD's own locks start above that range. Processes serialize against each other only if they agree
// on the index, so these values must never be renumbered.
const std::unordered_map<MutexType, uint8_t> MUTEX_TYPE_TO_KMD_LOCK_INDEX = {
    {MutexType::ARC_MSG, 16},
    {MutexType::REMOTE_ARC_MSG, 17},
    {MutexType::NON_MMIO, 18},
    {MutexType::MEM_BARRIER, 19},
    {MutexType::CHIP_IN_USE, 20},
    {MutexType::PCIE_DMA, 21},
};

uint8_t get_kmd_lock_index(MutexType mutex_type) {
    auto it = MUTEX_TYPE_TO_KMD_LOCK_INDEX.find(mutex_type);
    UMD_ASSERT(
        it != MUTEX_TYPE_TO_KMD_LOCK_INDEX.end(),
        error::RuntimeError,
        "Mutex " + MUTEX_TYPE_TO_STRING.at(mutex_type) + " has no KMD resource lock index assigned to it");
    return it->second;
}

// Holds two mutexes as one. This exists only for the move from shared memory locks to KMD resource locks: a process
// running an older UMD takes the shared memory lock alone and knows nothing about the KMD one, so during the
// transition both have to be taken or the two processes would not serialize. Once every client takes KMD locks, the
// shared memory half can go and the KMD mutex can be used on its own.
// The two are always taken in the same order, which is what keeps processes taking both from deadlocking against each
// other.
class CompositeMutex : public MutexInterface {
public:
    CompositeMutex(std::unique_ptr<MutexInterface> first, std::unique_ptr<MutexInterface> second) :
        first_(std::move(first)), second_(std::move(second)) {}

    void initialize() override {
        first_->initialize();
        second_->initialize();
    }

    // The first one is given back whenever taking the second does not work out, whether it reports the lock as taken
    // or throws. Otherwise it would stay held for the lifetime of the process: the caller never receives a
    // std::unique_lock when the constructor's lock() throws, so nothing is left to release it, and the shared memory
    // half only recovers itself when its holder dies.
    void lock() override {
        first_->lock();
        try {
            second_->lock();
        } catch (...) {
            first_->unlock();
            throw;
        }
    }

    void unlock() override {
        try {
            second_->unlock();
        } catch (...) {
            first_->unlock();
            throw;
        }
        first_->unlock();
    }

    std::optional<std::pair<pid_t, pid_t>> probe_lock(std::chrono::seconds timeout) override {
        std::optional<std::pair<pid_t, pid_t>> owner = first_->probe_lock(timeout);
        if (owner.has_value()) {
            return owner;
        }

        // Probing acquired the first one.
        try {
            owner = second_->probe_lock(timeout);
        } catch (...) {
            first_->unlock();
            throw;
        }
        if (owner.has_value()) {
            first_->unlock();
        }
        return owner;
    }

private:
    std::unique_ptr<MutexInterface> first_;
    std::unique_ptr<MutexInterface> second_;
};

// A lock no other process can contend for. A plain mutex serializes this process's threads just as well as a shared
// memory one, and leaves no file behind once the process is gone.
class InProcessMutex : public MutexInterface {
public:
    void initialize() override {}

    void lock() override {
        mutex_.lock();
        owner_tid_.store(current_tid(), std::memory_order_relaxed);
    }

    void unlock() override {
        owner_tid_.store(0, std::memory_order_relaxed);
        mutex_.unlock();
    }

    std::optional<std::pair<pid_t, pid_t>> probe_lock(std::chrono::seconds timeout) override {
        if (mutex_.try_lock_for(timeout)) {
            owner_tid_.store(current_tid(), std::memory_order_relaxed);
            return std::nullopt;
        }
        // Best effort: between the failed attempt and this read the owner may release the lock, or not have recorded
        // its tid yet, and is then unknown.
        const pid_t owner_tid = owner_tid_.load(std::memory_order_relaxed);
        if (owner_tid == 0) {
            return std::make_pair(pid_t{0}, pid_t{0});
        }
        return std::make_pair(getpid(), owner_tid);
    }

private:
    static pid_t current_tid() { return static_cast<pid_t>(::syscall(SYS_gettid)); }

    std::timed_mutex mutex_;
    std::atomic<pid_t> owner_tid_{0};
};

// Every mutex UMD has initialized, keyed by name. Names are made from the mutex type name, combined with the device
// number for chip specific ones.
struct MutexRegistry {
    std::mutex guard;
    std::unordered_map<std::string, std::unique_ptr<MutexInterface>> mutexes;
};

// Deliberately never destroyed: a mutex outliving the registry is far less trouble than one being torn down while
// another thread still holds it, or after the logger it reports through is gone. The OS reclaims the mappings and
// file descriptors at exit anyway.
MutexRegistry& get_registry() {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    static MutexRegistry* registry = new MutexRegistry();
    return *registry;
}

void add_mutex(const std::string& mutex_name, std::unique_ptr<MutexInterface> mutex) {
    MutexRegistry& registry = get_registry();

    {
        std::lock_guard<std::mutex> guard(registry.guard);
        if (registry.mutexes.find(mutex_name) != registry.mutexes.end()) {
            // Whoever needed this lock first has already set it up. Initializing is not owning, so this is the expected
            // outcome whenever more than one object works with the same device.
            return;
        }
    }

    // Setting a mutex up talks to the OS - a shared memory one takes a blocking flock, a KMD one opens the device - so
    // it happens outside the registry guard. Holding the guard across it would stall every other thread that only
    // wanted to look a lock up, and looking one up is on the IO paths.
    mutex->initialize();

    std::lock_guard<std::mutex> guard(registry.guard);
    // Another thread may have set the same lock up in the meantime, in which case theirs is the one in the registry and
    // ours is dropped here. Setting the same one up twice is harmless: every backend makes that safe.
    registry.mutexes.try_emplace(mutex_name, std::move(mutex));
}

// Probing has to acquire a free mutex to find out it was free, so release it again to keep probing a query.
std::optional<std::pair<pid_t, pid_t>> probe_and_release(MutexInterface& mutex) {
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(std::chrono::seconds(0));
    if (!owner.has_value()) {
        mutex.unlock();
    }
    return owner;
}

// Looking a mutex up hands out a reference into the registry, which stays valid because entries are only ever added.
// The registry guard is held for the lookup only, never while taking the mutex itself.
MutexInterface& get_initialized_mutex(const std::string& mutex_name) {
    MutexRegistry& registry = get_registry();
    std::lock_guard<std::mutex> guard(registry.guard);

    auto it = registry.mutexes.find(mutex_name);
    if (it == registry.mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    return *it->second;
}

}  // namespace

std::string to_string(MutexType mutex_type) { return MUTEX_TYPE_TO_STRING.at(mutex_type); }

void LockManager::initialize_mutex(MutexType mutex_type) {
    initialize_robust_mutex(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

void LockManager::initialize_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type, bool shared_across_processes) {
    // Every backend registers the lock under the same name, so the acquire and probe functions below find it whichever
    // one was chosen here.
    if (!shared_across_processes) {
        initialize_in_process_mutex(get_mutex_name(mutex_type, device_id, device_type));
    } else if (device_type == IODeviceType::PCIe) {
        initialize_kmd_mutex(mutex_type, device_id);
    } else {
        initialize_robust_mutex(get_mutex_name(mutex_type, device_id, device_type));
    }
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex(MutexType mutex_type) {
    return std::unique_lock(get_initialized_mutex(MUTEX_TYPE_TO_STRING.at(mutex_type)));
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    return std::unique_lock(get_initialized_mutex(get_mutex_name(mutex_type, device_id, device_type)));
}

std::unique_lock<MutexInterface> LockManager::try_acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    MutexInterface& mutex = get_initialized_mutex(get_mutex_name(mutex_type, device_id, device_type));

    // probe_lock() with no timeout is a non-blocking acquire: it takes a free lock, or reports the owner of a held one.
    if (mutex.probe_lock(std::chrono::seconds(0)).has_value()) {
        return std::unique_lock(mutex, std::defer_lock);
    }
    return std::unique_lock(mutex, std::adopt_lock);
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(MutexType mutex_type) {
    return probe_and_release(get_initialized_mutex(MUTEX_TYPE_TO_STRING.at(mutex_type)));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    return probe_and_release(get_initialized_mutex(get_mutex_name(mutex_type, device_id, device_type)));
}

void LockManager::initialize_in_process_mutex(const std::string& mutex_name) {
    add_mutex(mutex_name, std::make_unique<InProcessMutex>());
}

void LockManager::initialize_robust_mutex(const std::string& mutex_name) {
    add_mutex(mutex_name, std::make_unique<RobustMutex>(mutex_name));
}

void LockManager::initialize_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    // Registered under the name its shared memory half keeps, so that a process on an older UMD, which takes only that
    // half, contends on the very same lock.
    std::string mutex_name = get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe);
    add_mutex(
        mutex_name,
        std::make_unique<CompositeMutex>(
            std::make_unique<RobustMutex>(mutex_name),
            std::make_unique<KmdMutex>(pci_device_num, get_kmd_lock_index(mutex_type))));
}

}  // namespace tt::umd
