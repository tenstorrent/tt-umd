/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <unordered_map>

#include "umd/device/utils/robust_lock.h"

namespace tt::umd {

enum class MutexType {
    // Used to serialize communication with the ARC.
    ARC_MSG,
    // Used to serialize IO operations which are done directly through TTDevice. This is needed since it goes through a
    // single TLB.
    TT_DEVICE_IO,
    // Used to serialize non-MMIO operations over ethernet.
    NON_MMIO,
    // Used to serialize memory barrier operations.
    MEM_BARRIER,
    // Used for calling CEM tool.
    CREATE_ETH_MAP,
};

class LockManager {
public:
    std::unique_ptr<RobustLock> acquire_lock(MutexType mutex_type);
    std::unique_ptr<RobustLock> acquire_lock(MutexType mutex_type, int pci_device_id);
    std::unique_ptr<RobustLock> acquire_lock(std::string mutex_prefix, int pci_device_id);

private:
    std::unique_ptr<RobustLock> acquire_lock_internal(const std::string& mutex_name);

    // Const map of mutex names for each of the types listed in the enum.
    static const std::unordered_map<MutexType, std::string> MutexTypeToString;
};

}  // namespace tt::umd
