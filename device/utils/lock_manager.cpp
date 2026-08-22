// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/lock_manager.hpp"

#include <mutex>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::string to_string(MutexType mutex_type) { return LockManager::MUTEX_TYPE_TO_STRING.at(mutex_type); }

void LockManager::initialize_mutex(MutexType mutex_type) {
    initialize_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

void LockManager::initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = MUTEX_TYPE_TO_STRING.at(mutex_type) + "_" + std::to_string(device_id) + "_" +
                             DeviceTypeToString.at(device_type);
    initialize_mutex_internal(mutex_name);
}

void LockManager::clear_mutex(MutexType mutex_type) { clear_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type)); }

void LockManager::clear_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = MUTEX_TYPE_TO_STRING.at(mutex_type) + "_" + std::to_string(device_id) + "_" +
                             DeviceTypeToString.at(device_type);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(MutexType mutex_type) {
    return acquire_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = MUTEX_TYPE_TO_STRING.at(mutex_type) + "_" + std::to_string(device_id) + "_" +
                             DeviceTypeToString.at(device_type);
    return acquire_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex(const std::string& mutex_prefix, int device_id, IODeviceType device_type) {
    std::string mutex_name = mutex_prefix + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    initialize_mutex_internal(mutex_name);
}

void LockManager::clear_mutex(const std::string& mutex_prefix, int device_id, IODeviceType device_type) {
    std::string mutex_name = mutex_prefix + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(
    const std::string& mutex_prefix, int device_id, IODeviceType device_type) {
    std::string mutex_name = mutex_prefix + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    return acquire_mutex_internal(mutex_name);
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(MutexType mutex_type) {
    return probe_mutex_internal(MUTEX_TYPE_TO_STRING.at(mutex_type));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = MUTEX_TYPE_TO_STRING.at(mutex_type) + "_" + std::to_string(device_id) + "_" +
                             DeviceTypeToString.at(device_type);
    return probe_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) != mutexes.end()) {
        log_warning(LogUMD, "Mutex already initialized: {}", mutex_name);
        return;
    }

    mutexes.emplace(mutex_name, RobustMutex(mutex_name));
    mutexes.at(mutex_name).initialize();
}

void LockManager::clear_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        log_warning(LogUMD, "Mutex not initialized or already cleared: {}", mutex_name);
        return;
    }
    // The destructor will automatically close the underlying mutex.
    mutexes.erase(mutex_name);
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    return std::unique_lock(mutexes.at(mutex_name));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    RobustMutex& mutex = mutexes.at(mutex_name);
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(std::chrono::seconds(0));
    if (!owner.has_value()) {
        // The mutex was free, which means probing it took it. Release it so that probing stays a query.
        mutex.unlock();
    }
    return owner;
}

}  // namespace tt::umd
