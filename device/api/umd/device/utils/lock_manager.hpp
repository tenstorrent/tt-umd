// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/utils/mutex_interface.hpp"

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

// Name of a mutex type, for logging and diagnostics.
std::string to_string(MutexType mutex_type);

// Note that the returned std::unique_lock<MutexInterface> should never outlive the LockManager which holds
// underlying mutexes. Also note that clear_mutex doesn't need to be explicitly called, since the mutexes will all get
// cleared automatically when the LockManager goes out of scope. We could implement these lock such that initialization
// is not needed, and they are initialized every time they're locked, but since that communicates with the OS filesystem
// it might be slower do to it each time. This way, locking/unlocking should be faster.
class LockManager {
public:
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
    std::unique_lock<MutexInterface> acquire_mutex(MutexType mutex_type);

    // This set of functions is used to manage mutexes which are chip specific.
    void initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type);
    void clear_mutex(MutexType mutex_type, int device_id, IODeviceType device_type);
    std::unique_lock<MutexInterface> acquire_mutex(MutexType mutex_type, int device_id, IODeviceType device_type);

    // Reports whether a mutex is currently held, without holding it afterwards. Returns std::nullopt if the mutex was
    // free, otherwise the owning {pid, tid}. Since a free mutex has to be acquired to find that out, and is released
    // again right after, the answer is best effort and may be stale as soon as it is returned.
    std::optional<std::pair<pid_t, pid_t>> probe_mutex(MutexType mutex_type);
    std::optional<std::pair<pid_t, pid_t>> probe_mutex(MutexType mutex_type, int device_id, IODeviceType device_type);

private:
    void initialize_mutex_internal(const std::string& mutex_name);
    void clear_mutex_internal(const std::string& mutex_name);
    std::unique_lock<MutexInterface> acquire_mutex_internal(const std::string& mutex_name);
    std::optional<std::pair<pid_t, pid_t>> probe_mutex_internal(const std::string& mutex_name);

    // Maps from mutex name to an initialized mutex.
    // Mutex names are made from the mutex type name, combined with device number for chip specific ones.
    // Note that once LockManager is out of scope, all the mutexes will be cleared up automatically.
    std::unordered_map<std::string, std::unique_ptr<MutexInterface>> mutexes;
};

}  // namespace tt::umd
