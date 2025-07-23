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
    static void warm_reset(ARCH architecture, bool reset_m3 = false);

private:
    static void warm_reset_blackhole();

    static void warm_reset_wormhole(bool reset_m3);

    static uint64_t get_refclk_counter(TTDevice* tt_device);

    static void reinitialize(TTDevice* tt_device);
};

}  // namespace tt::umd
