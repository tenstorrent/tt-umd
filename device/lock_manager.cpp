/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/lock_manager.h"

#include <boost/interprocess/permissions.hpp>

#include "logger.hpp"

using namespace boost::interprocess;

namespace tt::umd {

const std::unordered_map<MutexType, std::string> LockManager::MutexTypeToString = {
    {MutexType::ARC_MSG, "TT_ARC_MSG"},
    // It is important that this mutex is named the same as corresponding fallback_tlb, this is due to the same tlb
    // index being used. This will be changed once we have a cleaner way to allocate TLBs instead of hardcoding fallback
    // tlbs.
    {MutexType::TT_DEVICE_IO, "SMALL_READ_WRITE_TLB"},
    {MutexType::NON_MMIO, "TT_NON_MMIO"},
    {MutexType::MEM_BARRIER, "TT_MEM_BARRIER"},
    {MutexType::CREATE_ETH_MAP, "CREATE_ETH_MAP"},
};

LockManager::LockManager() {}

LockManager::~LockManager() {
    for (const auto& [mutex_name, _] : mutexes) {
        named_mutex::remove(mutex_name.c_str());
    }
}

void LockManager::initialize_mutex(MutexType mutex_type, const bool clear_mutex) {
    initialize_mutex_internal(MutexTypeToString.at(mutex_type), clear_mutex);
}

void LockManager::clear_mutex(MutexType mutex_type) { clear_mutex_internal(MutexTypeToString.at(mutex_type)); }

std::unique_lock<named_mutex> LockManager::get_mutex(MutexType mutex_type) {
    return get_mutex_internal(MutexTypeToString.at(mutex_type));
}

void LockManager::initialize_mutex(MutexType mutex_type, int pci_device_id, const bool clear_mutex) {
    std::string mutex_name = MutexTypeToString.at(mutex_type) + std::to_string(pci_device_id);
    initialize_mutex_internal(mutex_name, clear_mutex);
}

void LockManager::clear_mutex(MutexType mutex_type, int pci_device_id) {
    std::string mutex_name = MutexTypeToString.at(mutex_type) + std::to_string(pci_device_id);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<named_mutex> LockManager::get_mutex(MutexType mutex_type, int pci_device_id) {
    std::string mutex_name = MutexTypeToString.at(mutex_type) + std::to_string(pci_device_id);
    return get_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex(std::string mutex_prefix, int pci_device_id, const bool clear_mutex) {
    std::string mutex_name = mutex_prefix + std::to_string(pci_device_id);
    initialize_mutex_internal(mutex_name, clear_mutex);
}

void LockManager::clear_mutex(std::string mutex_prefix, int pci_device_id) {
    std::string mutex_name = mutex_prefix + std::to_string(pci_device_id);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<named_mutex> LockManager::get_mutex(std::string mutex_prefix, int pci_device_id) {
    std::string mutex_name = mutex_prefix + std::to_string(pci_device_id);
    return get_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex_internal(const std::string& mutex_name, const bool clear_mutex) {
    if (mutexes.find(mutex_name) != mutexes.end()) {
        log_warning(LogSiliconDriver, "Mutex already initialized: {}", mutex_name);
        return;
    }

    // Store old mask and clear processes umask.
    // This will have an effect that the created files which back up named mutexes will have all permissions. This is
    // important to avoid permission issues between processes.
    auto old_umask = umask(0);

    if (clear_mutex) {
        named_mutex::remove(mutex_name.c_str());
    }
    permissions unrestricted_permissions;
    unrestricted_permissions.set_unrestricted();
    mutexes.emplace(
        mutex_name, std::make_unique<named_mutex>(open_or_create, mutex_name.c_str(), unrestricted_permissions));

    // Restore old mask.
    umask(old_umask);
}

void LockManager::clear_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        log_warning(LogSiliconDriver, "Mutex not initialized or already cleared: {}", mutex_name);
        return;
    }
    mutexes.erase(mutex_name);
    named_mutex::remove(mutex_name.c_str());
}

std::unique_lock<named_mutex> LockManager::get_mutex_internal(const std::string& mutex_name) {
    if (mutexes.find(mutex_name) == mutexes.end()) {
        throw std::runtime_error("Mutex not initialized: " + mutex_name);
    }
    return std::unique_lock<named_mutex>(*mutexes[mutex_name]);
}

}  // namespace tt::umd
