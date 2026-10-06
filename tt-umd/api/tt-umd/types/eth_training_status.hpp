// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

// Represents the status of the ETH core.
enum class EthTrainingStatus {
    IN_PROGRESS = 0,
    SUCCESS = 1,
    FAIL = 2,
    NOT_CONNECTED = 3,  // Maybe unconnected, not guaranteed. Detecting eth connection is unreliable.
};

}  // namespace tt::umd
