// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

/// Handler for host-to-device communication over a remote transport.
class RemoteCommunication;

/**
 * @defgroup tt_remote_interface RemoteInterface
 * @{
 *
 * @brief Remote device access and synchronization.
 *
 * Available when the device is reachable remotely via a local MMIO chip.
 * Provides access to the underlying communication handler and a barrier to
 * ensure all in-flight remote writes have landed.
 *
 * @optional
 *
 */

/**
 * @brief Remote transport capabilities for remote-connected devices.
 *
 * @optional
 */
class RemoteInterface {
public:
    virtual ~RemoteInterface() = default;

    /**
     * @brief Returns the underlying remote communication handler.
     */
    virtual RemoteCommunication *get_remote_communication() = 0;

    /**
     * @brief Blocks until all in-flight remote writes have reached the device.
     */
    virtual void wait_for_non_mmio_flush() = 0;
};

/** @} */  // end of tt_remote_interface group

}  // namespace tt::umd
