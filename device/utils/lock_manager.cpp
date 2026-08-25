// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/lock_manager.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

#include "umd/device/utils/error.hpp"
#include "umd/device/utils/kmd_mutex.hpp"
#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {

namespace {

// Names of the shared memory files backing each mutex.
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

// KMD resource lock index used for each chip specific mutex type. Indices 0..15 are reserved for ERISC cores (see
// KmdLockIndex), so UMD's own locks start above that range. Processes serialize against each other only if they agree
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

    void lock() override {
        first_->lock();
        second_->lock();
    }

    void unlock() override {
        second_->unlock();
        first_->unlock();
    }

    std::optional<std::pair<pid_t, pid_t>> probe_lock(std::chrono::seconds timeout) override {
        std::optional<std::pair<pid_t, pid_t>> owner = first_->probe_lock(timeout);
        if (owner.has_value()) {
            return owner;
        }
        // Probing acquired the first one. If the second turns out to be taken, give the first one back, so that a
        // failed probe leaves nothing held.
        owner = second_->probe_lock(timeout);
        if (owner.has_value()) {
            first_->unlock();
        }
        return owner;
    }

private:
    std::unique_ptr<MutexInterface> first_;
    std::unique_ptr<MutexInterface> second_;
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
    std::lock_guard<std::mutex> guard(registry.guard);

    if (registry.mutexes.find(mutex_name) != registry.mutexes.end()) {
        // Whoever needed this lock first has already set it up. Initializing is not owning, so this is the expected
        // outcome whenever more than one object works with the same device.
        return;
    }

    mutex->initialize();
    registry.mutexes.emplace(mutex_name, std::move(mutex));
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

void LockManager::initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    if (device_type == IODeviceType::PCIe) {
        initialize_kmd_mutex(mutex_type, device_id);
    } else {
        initialize_robust_mutex(get_mutex_name(mutex_type, device_id, device_type));
    }
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex(MutexType mutex_type) {
    return acquire_robust_mutex(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    if (device_type == IODeviceType::PCIe) {
        return acquire_kmd_mutex(mutex_type, device_id);
    }
    return acquire_robust_mutex(get_mutex_name(mutex_type, device_id, device_type));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(MutexType mutex_type) {
    return probe_robust_mutex(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    if (device_type == IODeviceType::PCIe) {
        return probe_kmd_mutex(mutex_type, device_id);
    }
    return probe_robust_mutex(get_mutex_name(mutex_type, device_id, device_type));
}

void LockManager::initialize_robust_mutex(const std::string& mutex_name) {
    add_mutex(mutex_name, std::make_unique<RobustMutex>(mutex_name));
}

std::unique_lock<MutexInterface> LockManager::acquire_robust_mutex(const std::string& mutex_name) {
    return std::unique_lock(get_initialized_mutex(mutex_name));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_robust_mutex(const std::string& mutex_name) {
    return probe_and_release(get_initialized_mutex(mutex_name));
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

std::unique_lock<MutexInterface> LockManager::acquire_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    return std::unique_lock(get_initialized_mutex(get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe)));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    return probe_and_release(get_initialized_mutex(get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe)));
}

}  // namespace tt::umd
