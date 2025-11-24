// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/blackhole_18_7_firmware_info_provider.hpp"

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

Blackhole_18_7_FirmwareInfoProvider::Blackhole_18_7_FirmwareInfoProvider(TTDevice* tt_device) :
    FirmwareInfoProvider(tt_device) {}

uint32_t Blackhole_18_7_FirmwareInfoProvider::get_max_clock_freq() const { return blackhole::AICLK_BUSY_VAL; }

}  // namespace tt::umd
