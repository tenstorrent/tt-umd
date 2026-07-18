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

void LockManager::initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = generate_mutex_name(mutex_type, device_id, device_type);
    if (mutexes.find(mutex_name) != mutexes.end()) {
        log_warning(LogUMD, "Mutex already initialized: {}", mutex_name);
        return;
    }

    mutexes.emplace(mutex_name, RobustMutex(mutex_name));
    mutexes.at(mutex_name).initialize();
}

void LockManager::clear_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = generate_mutex_name(mutex_type, device_id, device_type);
    if (mutexes.find(mutex_name) == mutexes.end()) {
        log_warning(LogUMD, "Mutex not initialized or already cleared: {}", mutex_name);
        return;
    }
    // The destructor will automatically close the underlying mutex.
    mutexes.erase(mutex_name);
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string mutex_name = generate_mutex_name(mutex_type, device_id, device_type);
    if (mutexes.find(mutex_name) == mutexes.end()) {
        UMD_THROW(error::RuntimeError, "Mutex not initialized: " + mutex_name);
    }
    return std::unique_lock(mutexes.at(mutex_name));
}

std::string LockManager::generate_mutex_name(MutexType mutex_type, int device_id, IODeviceType device_type) {
    return MUTEX_TYPE_TO_STRING.at(mutex_type) + "_" + std::to_string(device_id) + "_" +
           DeviceTypeToString.at(device_type);
}

}  // namespace tt::umd
