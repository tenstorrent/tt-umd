// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include "umd/device/utils/op_budget_timer.hpp"

namespace tt::umd {

/**
 * @brief Times individual operations against a per-op budget and reacts to overruns.
 *
 * Layers two caller-supplied behaviors on top of the bare OpBudgetTimer budget check:
 *   - an optional liveness veto (on_timeout): consulted only on an overrun. Returning false means
 *     "false positive, the op is healthy" and the overrun is ignored; returning true (or having no
 *     callback) confirms the overrun.
 *   - an overrun action (on_overrun): invoked on a confirmed overrun with the measured delta followed
 *     by whatever extra arguments the caller passes to record_and_check(). It is free to do anything,
 *     including throw — the guard itself knows nothing about exceptions or the operation's domain.
 *
 * The action and the per-call arguments are deliberately generic so the guard stays reusable: the
 * domain-specific reaction (e.g. throwing a typed timeout error with operation metadata) lives at the
 * call site, not in the guard.
 *
 * @tparam OnOverrun Callable invoked on a confirmed overrun as on_overrun(delta, args...).
 */
template <typename OnOverrun>
class OpTimeoutGuard {
public:
    OpTimeoutGuard(std::chrono::milliseconds budget, std::function<bool()> on_timeout, OnOverrun on_overrun) :
        timer_(budget), on_timeout_(std::move(on_timeout)), on_overrun_(std::move(on_overrun)) {}

    /**
     * @brief Checks one op (that started at `start`) and, on a confirmed overrun, runs the action.
     *
     * @param start Timestamp sampled immediately before the op began.
     * @param args Extra context forwarded to on_overrun after the measured delta.
     */
    template <typename... Args>
    void record_and_check(std::chrono::steady_clock::time_point start, Args&&... args) {
        if (!timer_.record_and_check(start)) {
            return;
        }
        // Capture the elapsed time the moment the overrun is detected, before the (possibly slow)
        // liveness probe runs, so the reported delta is the op's time and not the probe's.
        const auto delta =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
        // Over budget: an optional liveness probe gets to veto the reaction by declaring the op healthy.
        if (on_timeout_ && !on_timeout_()) {
            return;
        }
        on_overrun_(delta, std::forward<Args>(args)...);
    }

private:
    OpBudgetTimer timer_;
    std::function<bool()> on_timeout_;
    OnOverrun on_overrun_;
};

}  // namespace tt::umd
