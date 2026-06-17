// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

namespace tt::umd {

/**
 * @brief Times individual operations against a fixed per-op wall-clock budget.
 *
 * Construct once with a budget, then call record_and_check() for each op, passing the timestamp
 * sampled immediately before the op began. It returns whether that op overran the budget — nothing
 * more. The timer is stateless across ops and knows nothing about why the op is timed or what an
 * overrun means; the caller decides how to react (e.g. probe for liveness, then throw). This keeps
 * the timer a reusable, hardware-free, unit-testable primitive.
 */
class OpBudgetTimer {
public:
    explicit OpBudgetTimer(std::chrono::milliseconds budget) : budget_(budget) {}

    /**
     * @brief Checks one op (that started at `start`) against the budget.
     *
     * @param start Timestamp sampled immediately before the op began.
     * @return true if the op exceeded the budget, false otherwise.
     */
    bool record_and_check(std::chrono::steady_clock::time_point start) const {
        return std::chrono::steady_clock::now() - start >= budget_;
    }

private:
    std::chrono::milliseconds budget_;
};

}  // namespace tt::umd
