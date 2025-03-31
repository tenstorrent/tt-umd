/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/lock_manager.h"

#include <boost/interprocess/permissions.hpp>

#include "umd/device/tt_device/tt_device.h"

using namespace boost::interprocess;

namespace tt::umd {

const std::unordered_map<MutexType, std::string> LockManager::MutexTypeToString = {
    {MutexType::ARC_MSG, "TT_ARC_MSG"},
    // TBD to updated once we have more understanding, it may be important to name this mutex with the same name as the
    // corresponding fallback_tlb
    {MutexType::TT_DEVICE_IO, "SMALL_READ_WRITE_TLB"},
    {MutexType::NON_MMIO, "TT_NON_MMIO"},
    {MutexType::MEM_BARRIER, "TT_MEM_BARRIER"},
    {MutexType::CREATE_ETH_MAP, "CREATE_ETH_MAP"},
};

std::unordered_map<std::string, std::unique_ptr<boost::interprocess::named_mutex>> LockManager::mutexes;

LockManager::~LockManager() {
    // Clear out all mutexes and unlock them.
    while (!mutexes.empty()) {
        clear_mutex_internal(mutexes.begin()->first);
    }
}

void LockManager::initialize_mutex(MutexType mutex_type, const bool clear_mutex) {
    initialize_mutex_internal(MutexTypeToString.at(mutex_type), clear_mutex);
}

void LockManager::clear_mutex(MutexType mutex_type) { clear_mutex_internal(MutexTypeToString.at(mutex_type)); }

std::unique_lock<named_mutex> LockManager::get_mutex(MutexType mutex_type) {
    return get_mutex_internal(MutexTypeToString.at(mutex_type));
}

void LockManager::initialize_mutex(MutexType mutex_type, TTDevice* tt_device, const bool clear_mutex) {
    int device_num = tt_device->get_pci_device()->get_device_num();
    std::string mutex_name = MutexTypeToString.at(mutex_type) + std::to_string(device_num);
    initialize_mutex_internal(mutex_name, clear_mutex);
}

void LockManager::clear_mutex(MutexType mutex_type, TTDevice* tt_device) {
    int device_num = tt_device->get_pci_device()->get_device_num();
    std::string mutex_name = MutexTypeToString.at(mutex_type) + std::to_string(device_num);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<named_mutex> LockManager::get_mutex(MutexType mutex_type, TTDevice* tt_device) {
    int device_num = tt_device->get_pci_device()->get_device_num();
    std::string mutex_name = MutexTypeToString.at(mutex_type) + std::to_string(device_num);
    return get_mutex_internal(mutex_name);
}

void LockManager::initialize_mutex(std::string mutex_prefix, TTDevice* tt_device, const bool clear_mutex) {
    int device_num = tt_device->get_pci_device()->get_device_num();
    std::string mutex_name = mutex_prefix + std::to_string(device_num);
    initialize_mutex_internal(mutex_name, clear_mutex);
}

void LockManager::clear_mutex(std::string mutex_prefix, TTDevice* tt_device) {
    int device_num = tt_device->get_pci_device()->get_device_num();
    std::string mutex_name = mutex_prefix + std::to_string(device_num);
    clear_mutex_internal(mutex_name);
}

std::unique_lock<named_mutex> LockManager::get_mutex(std::string mutex_prefix, TTDevice* tt_device) {
    int device_num = tt_device->get_pci_device()->get_device_num();
    std::string mutex_name = mutex_prefix + std::to_string(device_num);
    return get_mutex_internal(mutex_name);
}

void LockManager::initialize_default_chip_mutexes(
    TTDevice* tt_device, TLBManager* tlb_manager, const bool clear_mutex) {
    // Initialize Dynamic TLB mutexes
    for (auto& tlb : tlb_manager->dynamic_tlb_config_) {
        LockManager::initialize_mutex(tlb.first, tt_device, clear_mutex);
    }

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        LockManager::initialize_mutex(MutexType::NON_MMIO, tt_device, clear_mutex);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic
    LockManager::initialize_mutex(MutexType::MEM_BARRIER, tt_device, clear_mutex);
}

void LockManager::initialize_mutex_internal(const std::string& mutex_name, const bool clear_mutex) {
    if (mutexes.find(mutex_name) != mutexes.end()) {
        throw std::runtime_error("Mutex already initialized: " + mutex_name);
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
        throw std::runtime_error("Mutex not initialized: " + mutex_name);
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
