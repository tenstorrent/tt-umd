// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

TTSimTTDevice::TTSimTTDevice() {
    throw std::runtime_error("Creating TTSimTTDevice without an underlying communication device is not supported.");
}

}  // namespace tt::umd
