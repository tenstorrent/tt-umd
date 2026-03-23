// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/utils/robust_mutex.hpp"

namespace tt::umd {

enum class MutexType {
    // Used to serialize communication with the ARC.
    ARC_MSG,
    // Used to serialize communication with the remote ARC over ethernet.
    REMOTE_ARC_MSG,
    // Used to serialize non-MMIO operations over ethernet.
    NON_MMIO,
    // Used to serialize memory barrier operations.
    MEM_BARRIER,
    // Used for calling CEM tool.
    CREATE_ETH_MAP,
    // Used for guarding against multiple users initializing the same chip.
    CHIP_IN_USE,
    // Used for guarding PCIe DMA operations against concurrent access from multiple processes.
    PCIE_DMA,
};

// Note that the returned std::unique_lock<RobustMutex> should never outlive the LockManager which holds underlying
// RobustMutexes. Also note that clear_mutex doesn't need to be explicitly called, since the mutexes will all get
// cleared automatically when the LockManager goes out of scope. We could implement these lock such that initialization
// is not needed, and they are initialized every time they're locked, but since that communicates with the OS filesystem
// it might be slower do to it each time. This way, locking/unlocking should be faster.
class LockManager {
public:
    // Maps MutexType enum values to their string names used in shared-memory lock names.
    inline static const std::unordered_map<MutexType, std::string> MUTEX_TYPE_TO_STRING = {
        {MutexType::ARC_MSG, "ARC_MSG"},
        {MutexType::REMOTE_ARC_MSG, "REMOTE_ARC_MSG"},
        {MutexType::NON_MMIO, "NON_MMIO"},
        {MutexType::MEM_BARRIER, "MEM_BARRIER"},
        {MutexType::CREATE_ETH_MAP, "CREATE_ETH_MAP"},
        {MutexType::CHIP_IN_USE, "CHIP_IN_USE"},
        {MutexType::PCIE_DMA, "PCIE_DMA"},
    };

    // Mutex types that are initialized per chip (combined with device_id + device_type).
    inline static const std::vector<MutexType> CHIP_SPECIFIC_MUTEX_TYPES = {
        MutexType::ARC_MSG,
        MutexType::REMOTE_ARC_MSG,
        MutexType::NON_MMIO,
        MutexType::MEM_BARRIER,
        MutexType::CHIP_IN_USE,
        MutexType::PCIE_DMA,
    };

    // Mutex types that are initialized system-wide (no device_id).
    inline static const std::vector<MutexType> SYSTEM_WIDE_MUTEX_TYPES = {
        MutexType::ARC_MSG,
        MutexType::CREATE_ETH_MAP,
    };

    // This set of functions is used to manage mutexes which are system wide and not chip specific.
    void initialize_mutex(MutexType mutex_type);
    void clear_mutex(MutexType mutex_type);
    std::unique_lock<RobustMutex> acquire_mutex(MutexType mutex_type);

    // This set of functions is used to manage mutexes which are chip specific.
    void initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type = IODeviceType::PCIe);
    void clear_mutex(MutexType mutex_type, int device_id, IODeviceType device_type = IODeviceType::PCIe);
    std::unique_lock<RobustMutex> acquire_mutex(
        MutexType mutex_type, int device_id, IODeviceType device_type = IODeviceType::PCIe);

    // This set of functions is used to manage mutexes which are chip specific. This variant accepts custom mutex name.
    void initialize_mutex(
        const std::string& mutex_prefix, int device_id, IODeviceType device_type = IODeviceType::PCIe);
    void clear_mutex(const std::string& mutex_prefix, int device_id, IODeviceType device_type = IODeviceType::PCIe);
    std::unique_lock<RobustMutex> acquire_mutex(
        const std::string& mutex_prefix, int device_id, IODeviceType device_type = IODeviceType::PCIe);

private:
    void initialize_mutex_internal(const std::string& mutex_name);
    void clear_mutex_internal(const std::string& mutex_name);
    std::unique_lock<RobustMutex> acquire_mutex_internal(const std::string& mutex_name);

    // Maps from mutex name to an initialized mutex.
    // Mutex names are made from mutex type name or directly mutex name combined with device number.
    // Note that once LockManager is out of scope, all the mutexes will be cleared up automatically.
    std::unordered_map<std::string, RobustMutex> mutexes;
};

}  // namespace tt::umd
