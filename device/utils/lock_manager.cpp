/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/utils/lock_manager.h"

#include "logger.hpp"

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

std::unique_ptr<RobustLock> LockManager::acquire_lock(MutexType mutex_type) {
    return acquire_lock_internal(MutexTypeToString.at(mutex_type));
}

std::unique_ptr<RobustLock> LockManager::acquire_lock(MutexType mutex_type, int pci_device_id) {
    std::string mutex_name = MutexTypeToString.at(mutex_type) + "_" + std::to_string(pci_device_id);
    return acquire_lock_internal(mutex_name);
}

std::unique_ptr<RobustLock> LockManager::acquire_lock(std::string mutex_prefix, int pci_device_id) {
    std::string mutex_name = mutex_prefix + "_" + std::to_string(pci_device_id);
    return acquire_lock_internal(mutex_name);
}

std::unique_ptr<RobustLock> LockManager::acquire_lock_internal(const std::string& mutex_name) {
    return std::make_unique<RobustLock>(mutex_name);
}

}  // namespace tt::umd
