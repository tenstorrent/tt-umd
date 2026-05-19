// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file config.cpp
 * @brief Implementation of UMD (User Mode Driver) logging initialization
 *
 * This file contains the initialization code for the UMD logging system.
 * It creates a static instance of the TT LoggerInitializer with specific
 * environment variable names for UMD logging configuration.
 */

#include "umd/device/logging/config.hpp"

#include <spdlog/common.h>

#include <tt-logger/tt-logger.hpp>

namespace tt::umd::logging {

/// Map our internal enum to spdlog's level enum.
spdlog::level::level_enum to_spdlog_level(level lvl) {
    switch (lvl) {
        case level::trace:
            return spdlog::level::trace;
        case level::debug:
            return spdlog::level::debug;
        case level::info:
            return spdlog::level::info;
        case level::warn:
            return spdlog::level::warn;
        case level::error:
            return spdlog::level::err;
        case level::critical:
            return spdlog::level::critical;
        case level::off:
            return spdlog::level::off;
    }
    return spdlog::level::info;  // fallback
}

/// Inverse of to_spdlog_level.
level from_spdlog_level(spdlog::level::level_enum lvl) {
    switch (lvl) {
        case spdlog::level::trace:
            return level::trace;
        case spdlog::level::debug:
            return level::debug;
        case spdlog::level::info:
            return level::info;
        case spdlog::level::warn:
            return level::warn;
        case spdlog::level::err:
            return level::error;
        case spdlog::level::critical:
            return level::critical;
        case spdlog::level::off:
            return level::off;
        default:
            return level::info;
    }
}

void set_level(level lvl) { ::tt::LoggerRegistry::instance().set_level(to_spdlog_level(lvl)); }

level get_level() { return from_spdlog_level(::tt::LoggerRegistry::instance().get(::tt::LogUMD)->level()); }

}  // namespace tt::umd::logging
