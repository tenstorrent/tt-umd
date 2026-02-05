// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/lock_manager.hpp"

#include <mutex>
#include <stdexcept>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>

namespace tt::umd {

const std::unordered_map<MutexType, std::string> LockManager::MutexTypeToString = {
    {MutexType::ARC_MSG, "ARC_MSG"},
    {MutexType::REMOTE_ARC_MSG, "REMOTE_ARC_MSG"},
    {MutexType::NON_MMIO, "NON_MMIO"},
    {MutexType::MEM_BARRIER, "MEM_BARRIER"},
    {MutexType::CREATE_ETH_MAP, "CREATE_ETH_MAP"},
    {MutexType::CHIP_IN_USE, "CHIP_IN_USE"},
    {MutexType::PCIE_DMA, "PCIE_DMA"},
};

const std::unordered_map<IODeviceType, std::string> LockManager::DeviceTypeToString = {
    {IODeviceType::PCIe, "PCIe"},
    {IODeviceType::JTAG, "JTAG"},
};

void LockManager::initialize_mutex(MutexType mutex_type) {
    initialize_mutex_internal(MutexTypeToString.at(mutex_type));
}

void LockManager::initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string const mutex_name =
        MutexTypeToString.at(mutex_type) + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    initialize_mutex_internal(mutex_name);
}

void LockManager::clear_mutex(MutexType mutex_type) { clear_mutex_internal(MutexTypeToString.at(mutex_type)); }

void LockManager::clear_mutex(MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string const mutex_name =
        MutexTypeToString.at(mutex_type) + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(MutexType mutex_type) {
    return acquire_mutex_internal(MutexTypeToString.at(mutex_type));
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(
    MutexType mutex_type, int device_id, IODeviceType device_type) {
    std::string const mutex_name =
        MutexTypeToString.at(mutex_type) + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    return acquire_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex(const std::string& mutex_prefix, int device_id, IODeviceType device_type) {
    std::string const mutex_name = mutex_prefix + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    initialize_mutex_internal(mutex_name);
}

void LockManager::clear_mutex(const std::string& mutex_prefix, int device_id, IODeviceType device_type) {
    std::string const mutex_name = mutex_prefix + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<RobustMutex> LockManager::acquire_mutex(
    const std::string& mutex_prefix, int device_id, IODeviceType device_type) {
    std::string const mutex_name = mutex_prefix + "_" + std::to_string(device_id) + "_" + DeviceTypeToString.at(device_type);
    return acquire_mutex_internal(mutex_name);
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
        throw std::runtime_error("Mutex not initialized: " + mutex_name);
    }
    return std::unique_lock(mutexes.at(mutex_name));
}

}  // namespace tt::umd
