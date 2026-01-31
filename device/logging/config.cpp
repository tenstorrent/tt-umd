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

#include <tt-logger/tt-logger.hpp>
#include "spdlog/common.h"

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

void set_level(level lvl) { ::tt::LoggerRegistry::instance().set_level(to_spdlog_level(lvl)); }

}  // namespace tt::umd::logging
