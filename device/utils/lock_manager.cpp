// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/lock_manager.hpp"

#include <mutex>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

#include "umd/device/utils/error.hpp"
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

}  // namespace

std::string to_string(MutexType mutex_type) { return MUTEX_TYPE_TO_STRING.at(mutex_type); }

void LockManager::initialize_mutex(MutexType mutex_type) {
    initialize_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

void LockManager::initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = get_mutex_name(mutex_type, device_id, device_type);
    initialize_mutex_internal(mutex_name);
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex(MutexType mutex_type) {
    return acquire_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = get_mutex_name(mutex_type, device_id, device_type);
    return acquire_mutex_internal(mutex_name);
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(MutexType mutex_type) {
    return probe_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = get_mutex_name(mutex_type, device_id, device_type);
    return probe_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) != mutexes.end()) {
        log_warning(LogUMD, "Mutex already initialized: {}", mutex_name);
        return;
    }

    std::unique_ptr<MutexInterface> mutex = std::make_unique<RobustMutex>(mutex_name);
    mutex->initialize();
    mutexes.emplace(mutex_name, std::move(mutex));
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    return std::unique_lock(*mutexes.at(mutex_name));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    MutexInterface& mutex = *mutexes.at(mutex_name);
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(std::chrono::seconds(0));
    if (!owner.has_value()) {
        // The mutex was free, which means probing it took it. Release it so that probing stays a query.
        mutex.unlock();
    }
    return owner;
}

}  // namespace tt::umd
