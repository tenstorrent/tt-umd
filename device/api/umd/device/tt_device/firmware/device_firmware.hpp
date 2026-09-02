// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

/**
 * @brief Interface to the device's management firmware.
 *
 * Owns the interaction with the firmware running on the device's management processor: waiting for
 * it to boot, issuing commands, and reading the state it publishes. TTDevice holds one and forwards
 * the firmware-backed parts of its API here; the implementations are built from protocol interfaces
 * rather than a TTDevice, so they carry no dependency on the facade.
 *
 * The interface starts empty on purpose. Each capability is added together with the TTDevice code
 * it replaces, so every addition is reviewable as a move.
 */
class DeviceFirmware {
public:
    virtual ~DeviceFirmware() = default;
};

}  // namespace tt::umd
