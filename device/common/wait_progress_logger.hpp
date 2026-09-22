// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

namespace tt::umd::utils {

// Periodically logs that a wait loop is still in progress, so a long poll (measured in seconds to
// minutes, not milliseconds) is visible instead of looking like a hang. Construct it once before
// the loop, with what it is waiting on and that wait's timeout, and call tick() once per
// iteration; tick() is a no-op except at most once per log_interval.
class WaitProgressLogger {
public:
    WaitProgressLogger(
        std::string what,
        std::chrono::milliseconds timeout,
        std::chrono::milliseconds log_interval = std::chrono::milliseconds(5'000))
        : what_(std::move(what)),
          timeout_(timeout),
          log_interval_(log_interval),
          start_(std::chrono::steady_clock::now()),
          last_logged_(start_) {}

    void tick() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_logged_ < log_interval_) {
            return;
        }
        last_logged_ = now;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_);
        log_debug(LogUMD, "Still waiting on {} ({}/{} ms elapsed).", what_, elapsed.count(), timeout_.count());
    }

private:
    std::string what_;
    std::chrono::milliseconds timeout_;
    std::chrono::milliseconds log_interval_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point last_logged_;
};

}  // namespace tt::umd::utils
