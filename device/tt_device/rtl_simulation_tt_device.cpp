// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"

namespace tt::umd {

RtlSimulationTTDevice::RtlSimulationTTDevice() {
    throw std::runtime_error(
        "Creating RtlSimulationTTDevice without an underlying communication device is not supported.");
}

}  // namespace tt::umd
