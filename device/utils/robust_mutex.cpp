/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/utils/robust_mutex.hpp"

#include "assert.hpp"
#include "umd/device/utils/robust_process_mutex.hpp"
#include "umd/device/utils/robust_system_mutex.hpp"

using namespace tt::umd;

std::unique_ptr<RobustMutex> RobustMutex::create(std::string_view mutex_name, MutexImplementationType type) {
    switch (type) {
        case MutexImplementationType::SYSTEM_WIDE:
            return std::make_unique<RobustSystemMutex>(mutex_name);
        case MutexImplementationType::PROCESS_LOCAL:
            return std::make_unique<RobustProcessMutex>(mutex_name);
        default:
            TT_THROW(fmt::format("Unknown mutex implementation type for mutex: {}", mutex_name));
    }
}
