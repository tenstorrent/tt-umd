/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#pragma once

#include <atomic>

#define SPDLOG_FMT_EXTERNAL
#include <spdlog/spdlog.h>

namespace tt::umd::logger {

/**
 * Parameters controlling the behavior of the logger.
 */
struct Options {
    bool log_to_stderr{true};
    std::string filename{};
    std::string pattern{"[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v"};
    spdlog::level::level_enum log_level{spdlog::level::debug};

    // TODO: this can be augmented as needed (log rotation, flush policy...)
};

/**
 * One-time initialization of the logger.
 *
 * If you don't call it, the logger will be initialized with default options the
 * first time a message is logged.
 */
void initialize(const Options& options = Options{});

/**
 * Macros for using the logger.
 */
#define UMD_TRACE(...)                                   \
    do {                                                 \
        ::tt::umd::logger::detail::ensure_initialized(); \
        SPDLOG_TRACE(__VA_ARGS__);                       \
    } while (0)

#define UMD_DEBUG(...)                                   \
    do {                                                 \
        ::tt::umd::logger::detail::ensure_initialized(); \
        SPDLOG_DEBUG(__VA_ARGS__);                       \
    } while (0)

#define UMD_INFO(...)                                    \
    do {                                                 \
        ::tt::umd::logger::detail::ensure_initialized(); \
        SPDLOG_INFO(__VA_ARGS__);                        \
    } while (0)

#define UMD_WARN(...)                                    \
    do {                                                 \
        ::tt::umd::logger::detail::ensure_initialized(); \
        SPDLOG_WARN(__VA_ARGS__);                        \
    } while (0)

#define UMD_ERROR(...)                                   \
    do {                                                 \
        ::tt::umd::logger::detail::ensure_initialized(); \
        SPDLOG_ERROR(__VA_ARGS__);                       \
    } while (0)

#define UMD_CRITICAL(...)                                \
    do {                                                 \
        ::tt::umd::logger::detail::ensure_initialized(); \
        SPDLOG_CRITICAL(__VA_ARGS__);                    \
    } while (0)

/**
 * This is not part of the API.
 */
namespace detail {
extern std::atomic_bool is_initialized;

inline void ensure_initialized() {
    if (!is_initialized.load(std::memory_order_acquire)) {
        initialize();
    }
}

}  // namespace detail

}  // namespace tt::umd::logger
