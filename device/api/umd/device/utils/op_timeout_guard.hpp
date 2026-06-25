// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <functional>
#include <utility>

namespace tt::umd {

/**
 * @brief Times individual operations against a per-op budget and reacts to overruns.
 *
 * For each op, call record_and_check() with the timestamp sampled immediately before the op began. If
 * the elapsed time exceeds the budget the guard reacts; otherwise it does nothing. A budget of 0
 * disables the check entirely (no op ever counts as an overrun), matching the convention of
 * utils::check_timeout.
 *
 * Two caller-supplied behaviors layer on top of the bare budget check:
 *   - an optional false-alarm veto (is_false_alarm): consulted only on an apparent overrun. Returning
 *     true means "false positive, the op is healthy" and the overrun is ignored; returning false (or
 *     having no callback) confirms the overrun.
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
    OpTimeoutGuard(std::chrono::milliseconds budget, std::function<bool()> is_false_alarm, OnOverrun on_overrun) :
        budget_(budget), is_false_alarm_(std::move(is_false_alarm)), on_overrun_(std::move(on_overrun)) {}

    /**
     * @brief Checks one op (that started at `start`) and, on a confirmed overrun, runs the action.
     *
     * @param start Timestamp sampled immediately before the op began.
     * @param args Extra context forwarded to on_overrun after the measured delta.
     */
    template <typename... Args>
    void record_and_check(std::chrono::steady_clock::time_point start, Args&&... args) {
        // A budget of 0 disables the check; otherwise the op is over budget only once the elapsed time
        // exceeds it (same convention as utils::check_timeout).
        if (budget_.count() == 0 || std::chrono::steady_clock::now() - start <= budget_) {
            return;
        }
        // Capture the elapsed time the moment the overrun is detected, before the (possibly slow)
        // false-alarm probe runs, so the reported delta is the op's time and not the probe's.
        const auto delta =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
        // Over budget: an optional probe gets to veto the reaction by declaring the op a false alarm.
        if (is_false_alarm_ && is_false_alarm_()) {
            return;
        }
        on_overrun_(delta, std::forward<Args>(args)...);
    }

private:
    std::chrono::milliseconds budget_;
    std::function<bool()> is_false_alarm_;
    OnOverrun on_overrun_;
};

}  // namespace tt::umd
