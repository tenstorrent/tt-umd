// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {
class TTSimTTDevice : public TTDevice {
public:
    TTSimTTDevice();
};
}  // namespace tt::umd
