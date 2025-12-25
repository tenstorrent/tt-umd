// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"

namespace tt::umd {

RtlSimulationTTDevice::RtlSimulationTTDevice() {
    TT_THROW("Creating RtlSimulationTTDevice without an underlying communication device is not supported.");
}

}  // namespace tt::umd
