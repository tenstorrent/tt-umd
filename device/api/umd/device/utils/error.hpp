// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdexcept>
#include <string>

#include "umd/device/utils/error_detail.hpp"

namespace tt::umd::error {

struct NoData {};

struct RuntimeError : public UmdError<NoData> {
    explicit RuntimeError(const std::string& message) : UmdError<NoData>(message, {}) {}
};

/**
 * @brief Exception thrown when a SIGBUS signal is intercepted.
 * This indicates a hardware access error, likely due to a reset or
 * hanging device while accessing mapped memory.
 */
class SigbusError : public std::runtime_error {
public:
    explicit SigbusError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace tt::umd::error
