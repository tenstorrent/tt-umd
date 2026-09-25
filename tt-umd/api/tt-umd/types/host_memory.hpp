// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

/**
 * @brief Device permissions for a pinned host-memory mapping.
 *
 * READ_ONLY prevents the device from writing the mapping. It does not restrict
 * host access to the underlying virtual memory.
 */
enum class DeviceBufferAccess {
    READ_WRITE,
    READ_ONLY,
};

}  // namespace tt::umd
