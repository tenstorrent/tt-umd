// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

/**
 * @file logger_init.cpp
 * @brief Implementation of UMD (User Mode Driver) logging initialization
 *
 * This file contains the initialization code for the UMD logging system.
 * It creates a static instance of the TT LoggerInitializer with specific
 * environment variable names for UMD logging configuration.
 */

#include <tt-logger/tt-logger-initializer.hpp>

namespace umd_logging {
/**
 * @brief Static instance of LoggerInitializer for UMD logging
 *
 * This static instance initializes the logging system with UMD-specific
 * environment variables:
 * - TT_UMD_LOGGER_FILE: Controls the log file path for UMD logging
 * - TT_UMD_LOGGER_LEVEL: Controls the log level for UMD logging
 *
 * The logger will be initialized when this translation unit is loaded,
 * setting up either file-based or console-based logging depending on
 * the environment variable configuration.
 */
static tt::LoggerInitializer loggerInitializer("TT_UMD_LOGGER_FILE", "TT_UMD_LOGGER_LEVEL");
}  // namespace umd_logging
