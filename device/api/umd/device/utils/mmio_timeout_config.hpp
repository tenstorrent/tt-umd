// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>

#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

/**
 * @brief Runtime-configurable per-op budget for host-side MMIO (TLB-mapped) transfers.
 *
 * The per-op MMIO timeout is still being tuned, so the budget is settable at runtime via this small
 * setter/getter instead of a constant. A programmatic setter (rather than an env var) is discoverable,
 * strongly typed, testable, and does not leak across processes; callers set it explicitly from their
 * code (or from Python). Once the right default settles this class can be deleted in favor of the
 * timeout::MMIO_OP_TIMEOUT constant. Defaults to timeout::MMIO_OP_TIMEOUT.
 */
class MmioTimeoutConfig {
public:
    static void set_op_timeout(std::chrono::milliseconds timeout) {
        op_timeout_ms_.store(timeout.count(), std::memory_order_relaxed);
    }

    static std::chrono::milliseconds get_op_timeout() {
        return std::chrono::milliseconds(op_timeout_ms_.load(std::memory_order_relaxed));
    }

private:
    inline static std::atomic<std::chrono::milliseconds::rep> op_timeout_ms_{timeout::MMIO_OP_TIMEOUT.count()};
};

}  // namespace tt::umd
