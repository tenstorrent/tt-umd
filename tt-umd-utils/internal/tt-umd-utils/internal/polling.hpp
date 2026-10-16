// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>

#include "tt-umd-utils/error.hpp"
#include "tt-umd-utils/internal/wait_progress_logger.hpp"

namespace tt::umd::utils {

/**
 * Checks if `timeout` amount of time has elapsed since `start_time`.
 * @param start_time Point in time when the measured event started.
 * @param timeout Time expected for event to complete.
 * @returns True if `timeout` amount of time has elapsed since `start_time`.
 */
inline bool check_timeout(
    const std::chrono::steady_clock::time_point start_time, const std::chrono::milliseconds timeout) noexcept {
    // A timeout of 0 can never time out.
    if (timeout.count() == 0) {
        return false;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    return elapsed > timeout;
}

/**
 * Adaptive polling loop. Busy-polls `predicate` for `busy_poll_window`, then sleeps
 * `poll_interval` between checks until the predicate returns true or `timeout` elapses.
 *
 * @param predicate Callable returning bool; polled until it returns true.
 * @param timeout Maximum total time to wait.
 * @param busy_poll_window How long to spin before backing off to sleep-based polling.
 * @param poll_interval Sleep duration between checks once past the busy window.
 * @param progress_what What is being waited on, e.g. "ARC firmware on device 0 to become ready".
 *        When set, a "Still waiting on ..." line is logged periodically (see WaitProgressLogger)
 *        so a long wait does not look like a hang. Leave unset for waits that are expected to be short.
 * @returns True if predicate became true before timeout, false otherwise.
 *          Caller is responsible for handling the timeout case.
 */
template <typename Predicate>
inline bool poll_until(
    Predicate predicate,
    const std::chrono::milliseconds timeout,
    const std::chrono::microseconds busy_poll_window,
    const std::chrono::microseconds poll_interval,
    const std::optional<std::string_view> progress_what = std::nullopt) {
    // Ensure the predicate is callable.
    static_assert(std::is_invocable_v<Predicate>, "poll_until: The predicate provided must be callable.");

    // Enforce strict bool return type.
    using ReturnType = std::invoke_result_t<Predicate>;
    static_assert(std::is_same_v<ReturnType, bool>, "poll_until: Predicate must return 'bool'.");

    const auto start = std::chrono::steady_clock::now();
    std::optional<WaitProgressLogger> progress_logger;
    if (progress_what.has_value()) {
        progress_logger.emplace(
            std::string(progress_what.value()), timeout, WaitProgressLogger::DEFAULT_LOG_INTERVAL, start);
    }
    while (!predicate()) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - start;
        if (elapsed > timeout) {
            return false;
        }
        if (progress_logger.has_value()) {
            progress_logger->tick(now);
        }
        if (elapsed > busy_poll_window) {
            std::this_thread::sleep_for(poll_interval);
        }
    }
    return true;
}

enum class TimeoutAction { Throw, Return };

/**
 * Throw std::runtime_error or return true if `timeout` amount of time has elapsed since `start_time`.
 * @param start_time Point in time when the measured event started.
 * @param timeout Time expected for event to complete.
 * @param error_msg Error message to log or pass to RuntimeError.
 * @param action Decide which action (throw or return false) is done when timeout elapses.
 */
inline bool check_timeout(
    const std::chrono::steady_clock::time_point start_time,
    const std::chrono::milliseconds timeout,
    const std::string& error_msg,
    TimeoutAction action = TimeoutAction::Throw) {
    bool timed_out = check_timeout(start_time, timeout);
    if (timed_out) {
        auto error = UMD_THROW_OR_RETURN(action == TimeoutAction::Throw, error::RuntimeError, error_msg);
        log_warning(LogUMD, error.message());
    }
    return timed_out;
}

}  // namespace tt::umd::utils
