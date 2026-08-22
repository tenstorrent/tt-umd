// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/lock_manager.hpp"

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

// Probing has to acquire a free mutex to find out it was free, so release it again to keep probing a query.
std::optional<std::pair<pid_t, pid_t>> probe_and_release(MutexInterface& mutex) {
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(std::chrono::seconds(0));
    if (!owner.has_value()) {
        mutex.unlock();
    }
    return owner;
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

void LockManager::clear_mutex(MutexType mutex_type) { clear_robust_mutex(MUTEX_TYPE_TO_STRING.at(mutex_type)); }

void LockManager::clear_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    if (device_type == IODeviceType::PCIe) {
        clear_kmd_mutex(mutex_type, device_id);
    } else {
        clear_robust_mutex(get_mutex_name(mutex_type, device_id, device_type));
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

void LockManager::clear_robust_mutex(const std::string& mutex_name) { remove_mutex(mutex_name); }

std::unique_lock<MutexInterface> LockManager::acquire_robust_mutex(const std::string& mutex_name) {
    return std::unique_lock(get_mutex(mutex_name));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_robust_mutex(const std::string& mutex_name) {
    return probe_and_release(get_mutex(mutex_name));
}

void LockManager::initialize_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    add_mutex(
        get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe),
        std::make_unique<KmdMutex>(pci_device_num, get_kmd_lock_index(mutex_type)));
}

void LockManager::clear_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    remove_mutex(get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe));
}

std::unique_lock<MutexInterface> LockManager::acquire_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    return std::unique_lock(get_mutex(get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe)));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_kmd_mutex(MutexType mutex_type, int pci_device_num) {
    return probe_and_release(get_mutex(get_mutex_name(mutex_type, pci_device_num, IODeviceType::PCIe)));
}

void LockManager::add_mutex(const std::string& mutex_name, std::unique_ptr<MutexInterface> mutex) {
    if (mutexes.find(mutex_name) != mutexes.end()) {
        log_warning(LogUMD, "Mutex already initialized: {}", mutex_name);
        return;
    }

    mutex->initialize();
    mutexes.emplace(mutex_name, std::move(mutex));
}

void LockManager::remove_mutex(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        log_warning(LogUMD, "Mutex not initialized or already cleared: {}", mutex_name);
        return;
    }
    // The destructor will automatically close the underlying mutex.
    mutexes.erase(mutex_name);
}

MutexInterface& LockManager::get_mutex(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    return *mutexes.at(mutex_name);
}

}  // namespace tt::umd
