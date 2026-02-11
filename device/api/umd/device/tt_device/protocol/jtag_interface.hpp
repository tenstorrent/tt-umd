/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/jtag/jtag_device.hpp"

namespace tt::umd {

class JtagInterface {
public:
    virtual JtagDevice *get_jtag_device() = 0;
};

}  // namespace tt::umd
