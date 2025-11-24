// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/firmware/firmware_info_provider.hpp"

namespace tt::umd {

/* This class captures Wormhole firmware up to version 18.7.0.
 * Firmware releases with this and older versions don't have max AICLK inside
 * new telemetry for Wormhole so that has to be read from SM bus telemetry.
 */
class Wormhole_18_7_FirmwareInfoProvider : public FirmwareInfoProvider {
public:
    Wormhole_18_7_FirmwareInfoProvider(TTDevice* tt_device);

    uint32_t get_max_clock_freq() const override;
};

}  // namespace tt::umd
