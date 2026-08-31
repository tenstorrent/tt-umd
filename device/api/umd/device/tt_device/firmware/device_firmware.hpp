// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

#include "umd/device/types/noc_id.hpp"

namespace tt::umd {

/**
 * @brief Interface to the device's management firmware.
 *
 * Owns the interaction with the firmware running on the device's management processor: waiting for
 * it to boot, issuing commands, and reading the state it publishes. TTDevice forwards the
 * firmware-backed parts of its API here; the implementations are built from protocol interfaces
 * rather than a TTDevice, so they carry no dependency on the facade.
 *
 * The interface grows one capability at a time, each added together with the TTDevice code it
 * replaces, so every addition is reviewable as a move.
 */
class DeviceFirmware {
public:
    virtual ~DeviceFirmware() = default;

    /**
     * @brief Performs the firmware initialization.
     *
     * Blocks until the management firmware reports it has booted.
     *
     * @param timeout_ms Maximum time to wait for the firmware to become ready.
     * @param noc_id NOC to route the status reads over.
     * @throws error::FirmwareStartupError if the firmware does not come up within the timeout.
     */
    virtual void init_firmware(
        std::chrono::milliseconds timeout_ms, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;
};

}  // namespace tt::umd
