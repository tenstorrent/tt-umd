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
    MutexRegistry& registry = get_registry();
    std::lock_guard<std::mutex> guard(registry.guard);

    if (registry.mutexes.find(mutex_name) != registry.mutexes.end()) {
        // Whoever needed this lock first has already set it up. Initializing is not owning, so this is the expected
        // outcome whenever more than one object works with the same device.
        return;
    }

    std::unique_ptr<MutexInterface> mutex = std::make_unique<RobustMutex>(mutex_name);
    mutex->initialize();
    registry.mutexes.emplace(mutex_name, std::move(mutex));
}

std::unique_lock<MutexInterface> LockManager::acquire_mutex_internal(const std::string& mutex_name) {
    return std::unique_lock(get_initialized_mutex(mutex_name));
}

std::optional<std::pair<pid_t, pid_t>> LockManager::probe_mutex_internal(const std::string& mutex_name) {
    MutexInterface& mutex = get_initialized_mutex(mutex_name);
    std::optional<std::pair<pid_t, pid_t>> owner = mutex.probe_lock(std::chrono::seconds(0));
    if (!owner.has_value()) {
        // The mutex was free, which means probing it took it. Release it so that probing stays a query.
        mutex.unlock();
    }
    return owner;
}

}  // namespace tt::umd
