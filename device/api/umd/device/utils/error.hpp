// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error_detail.hpp"

namespace tt::umd::error {

struct CoreExceptionData {
public:
    xy_pair core;
};

struct ETHHeartbeatFailureData : public CoreExceptionData {
    uint32_t postcode;
    uint32_t heartbeat_value;
};

class ETHHeartbeatError : public UmdError<ETHHeartbeatFailureData> {
public:
    explicit ETHHeartbeatError(tt_xy_pair eth_core, uint32_t postcode, uint32_t heartbeat_value);
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
