// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>

#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

/**
 * @brief Runtime-configurable per-op budget for host-side MMIO (TLB-mapped) transfers.
 *
 * Defaults to timeout::MMIO_OP_TIMEOUT; set_op_timeout overrides it at runtime. A non-positive budget
 * disables the per-op MMIO timeout.
 */
class MmioTimeoutConfig {
public:
    static void set_op_timeout(std::chrono::milliseconds timeout) {
        // Clamp non-positive budgets to 0 (disabled); a negative budget would otherwise make every op
        // look over budget.
        op_timeout_ms_.store(std::max<std::chrono::milliseconds::rep>(0, timeout.count()), std::memory_order_relaxed);
    }

    static std::chrono::milliseconds get_op_timeout() {
        return std::chrono::milliseconds(op_timeout_ms_.load(std::memory_order_relaxed));
    }

private:
    inline static std::atomic<std::chrono::milliseconds::rep> op_timeout_ms_{timeout::MMIO_OP_TIMEOUT.count()};
};

}  // namespace tt::umd
