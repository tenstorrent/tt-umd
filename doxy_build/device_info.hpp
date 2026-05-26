// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class DeviceInfo {
public:
    virtual ~DeviceInfo() = default;
    virtual tt::ARCH get_arch() const = 0;
    virtual IODeviceType get_device_type() const = 0;
    virtual int get_device_id() const = 0;
    virtual bool is_remote() const = 0;
};

}  // namespace tt::umd
