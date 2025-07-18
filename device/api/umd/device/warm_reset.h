/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "api/umd/device/tt_device/tt_device.h"

namespace tt::umd {

class WarmReset {
public:
    static void warm_reset(ARCH architecture);

private:
    static void warm_reset_blackhole();

    static void warm_reset_wormhole();

    static uint64_t check_refclk(TTDevice* tt_device);
};

}  // namespace tt::umd
