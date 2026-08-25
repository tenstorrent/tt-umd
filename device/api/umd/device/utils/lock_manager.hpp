// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <mutex>
#include <optional>
#include <string>
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

// The locks handed out here are process wide, and the underlying ones system wide, so LockManager holds them in a
// single registry for the whole process rather than one per owner. A lock is initialized once, by whoever needs it
// first, and lives until the process exits.
// Besides being the honest model, this bounds how many locks a process can have open at a time. An initialized mutex
// may hold a file descriptor for as long as it lives, so a registry per owner meant the same lock was opened once per
// chip, per device and per cluster, and a process working with many chips could run itself out of descriptors.
// We could implement these locks such that initialization is not needed, and they are initialized every time they're
// locked, but since that communicates with the OS it might be slower to do it each time. This way, locking/unlocking
// should be faster.
// Locks are only ever added to the registry, never removed, and that is what makes the returned
// std::unique_lock<MutexInterface> safe to hold for as long as the caller likes: the mutex it refers to stays where it
// is for the lifetime of the process. Anything that removed locks again would leave every outstanding one dangling.
//
// Chip specific locks on a PCIe device are backed by a KMD resource lock, so that processes which share the device but
// not /dev/shm still serialize against each other. Everything else - system wide locks and locks on a JTAG device - is
// backed by RobustMutex, since a KMD resource lock exists only per local PCIe device.
class LockManager {
public:
    LockManager() = delete;

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
    static void initialize_mutex(MutexType mutex_type);
    static std::unique_lock<MutexInterface> acquire_mutex(MutexType mutex_type);

    // This set of functions is used to manage mutexes which are chip specific.
    static void initialize_mutex(MutexType mutex_type, int device_id, IODeviceType device_type);
    static std::unique_lock<MutexInterface> acquire_mutex(
        MutexType mutex_type, int device_id, IODeviceType device_type);

    // Reports whether a mutex is currently held, without holding it afterwards. Returns the owning {pid, tid} if it is
    // held, and std::nullopt if it is not - which covers both a mutex nobody had taken and one whose owner died holding
    // it, since probing a shared memory mutex recovers it from a dead owner rather than reporting it as held. Since a
    // free mutex has to be acquired to find that out, and is released again right after, the answer is best effort and
    // may be stale as soon as it is returned.
    static std::optional<std::pair<pid_t, pid_t>> probe_mutex(MutexType mutex_type);
    static std::optional<std::pair<pid_t, pid_t>> probe_mutex(
        MutexType mutex_type, int device_id, IODeviceType device_type);

private:
    // Locks backed by a shared memory RobustMutex, addressed by their name.
    static void initialize_robust_mutex(const std::string& mutex_name);
    static std::unique_lock<MutexInterface> acquire_robust_mutex(const std::string& mutex_name);
    static std::optional<std::pair<pid_t, pid_t>> probe_robust_mutex(const std::string& mutex_name);

    // Locks backed by a KMD resource lock on the device owning the lock table. They take the shared memory lock as
    // well, for as long as clients on an older UMD exist which know only about that one.
    static void initialize_kmd_mutex(MutexType mutex_type, int pci_device_num);
    static std::unique_lock<MutexInterface> acquire_kmd_mutex(MutexType mutex_type, int pci_device_num);
    static std::optional<std::pair<pid_t, pid_t>> probe_kmd_mutex(MutexType mutex_type, int pci_device_num);
};

}  // namespace tt::umd
