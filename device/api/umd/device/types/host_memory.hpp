// SPDX-FileCopyrightText: © 2026 Tenstorrent USA, Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

/**
 * @brief Device permissions for a pinned host-memory mapping.
 *
 * ReadOnly prevents the device from writing the mapping. It does not restrict
 * host access to the underlying virtual memory.
 */
enum class DeviceBufferAccess {
    ReadWrite,
    ReadOnly,
};

}  // namespace tt::umd
