// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

namespace tt::umd::utils {

// Periodically logs that a wait loop is still in progress, so a long poll (measured in seconds to
// minutes, not milliseconds) is visible instead of looking like a hang. Construct it once
// immediately before the loop, with what it is waiting on and that wait's timeout, and call tick()
// once per iteration; it logs at most once per log_interval. A wait that finishes well within
// log_interval never logs, since tick() is first called right after construction.
class WaitProgressLogger {
public:
    WaitProgressLogger(
        std::string what,
        std::chrono::milliseconds timeout,
        std::chrono::milliseconds log_interval = std::chrono::milliseconds(5'000)) :
        what_(std::move(what)),
        timeout_(timeout),
        log_interval_(log_interval),
        start_(std::chrono::steady_clock::now()),
        last_logged_(start_) {}

    // `now` defaults to the real clock for production call sites; tests pass an explicit time
    // point so the throttling can be exercised deterministically, without sleeping, matching the
    // convention OpTimeoutGuard::record_and_check uses for the same reason. Returns whether it
    // logged, so tests can assert on the decision without capturing log output.
    bool tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        if (now - last_logged_ < log_interval_) {
            return false;
        }
        last_logged_ = now;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_);
        // timeout_ is not always a fixed total for the whole wait (e.g. wait_eth_core_training is
        // handed a shared budget that shrinks core by core), so it is reported as time remaining
        // rather than as a denominator that would look constant but isn't.
        const auto remaining = std::max(std::chrono::milliseconds(0), timeout_ - elapsed);
        log_info(
            LogUMD,
            "Still waiting on {} ({} ms elapsed, {} ms budget left).",
            what_,
            elapsed.count(),
            remaining.count());
        return true;
    }

private:
    std::string what_;
    std::chrono::milliseconds timeout_;
    std::chrono::milliseconds log_interval_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point last_logged_;
};

}  // namespace tt::umd::utils
