/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "logger_.hpp"  // TODO: rename after logger.hpp is removed

#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

#include <mutex>

namespace tt::umd::logger {

void initialize(const Options& options) {
    static std::mutex mutex;
    std::scoped_lock lock{mutex};

    if (detail::is_initialized.load(std::memory_order_relaxed)) {
        return;
    }

    std::vector<spdlog::sink_ptr> sinks;

    if (options.log_to_stderr) {
        auto stderr_sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        sinks.push_back(stderr_sink);
    }

    if (!options.filename.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(options.filename);
        sinks.push_back(file_sink);
    }

    auto logger = std::make_shared<spdlog::logger>("UMD", sinks.begin(), sinks.end());
    logger->set_level(options.log_level);
    logger->set_pattern(options.pattern);

    spdlog::set_default_logger(logger);
    detail::is_initialized.store(true, std::memory_order_release);
}

namespace detail {
std::atomic_bool is_initialized = false;
}

}  // namespace tt::umd::logger
