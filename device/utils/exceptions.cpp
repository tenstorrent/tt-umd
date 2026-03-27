// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/exceptions.hpp"

#include <fmt/format.h>

#include <stdexcept>
#include <string>

namespace tt::umd {
ETHHeartbeatError::ETHHeartbeatError(tt_xy_pair eth_core, uint32_t postcode, uint32_t heartbeat_value) :
    UmdError<ETHHeartbeatFailureData>(
        fmt::format(
            "Ethernet heartbeat error on core {}: postcode={:#x}, heartbeat={:#x}",
            eth_core.str(),
            postcode,
            heartbeat_value),
        ETHHeartbeatFailureData{{.core = eth_core}, postcode, heartbeat_value}) {}
}  // namespace tt::umd
