// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/firmware/firmware_info_provider.hpp"

namespace tt::umd {

/* This class captures Blackhole firmware up to version 18.7.0.
 * In this firmware release there was not ASIC id information available,
 * as well as maximum possible AICLK on the device. So these functions return
 * placeholder values in this class.
 * Release: https://github.com/tenstorrent/tt-firmware/releases/tag/v18.8.0
 */
class Blackhole_18_7_FirmwareInfoProvider : public FirmwareInfoProvider {
public:
    Blackhole_18_7_FirmwareInfoProvider(TTDevice* tt_device, bool use_noc1);

    uint32_t get_max_clock_freq(bool use_noc1) const override;
};

}  // namespace tt::umd
