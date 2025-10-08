/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "api/umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

class WarmReset {
public:
    static void warm_reset(bool reset_m3 = false);

    static void ubb_warm_reset();

private:
    static constexpr int POST_RESET_WAIT = 2;

    static void warm_reset_blackhole();

    static void warm_reset_wormhole(bool reset_m3);

    static void wormhole_ubb_ipmi_reset(int ubb_num, int dev_num, int op_mode, int reset_time);

    static void ubb_wait_for_driver_load();
};

}  // namespace tt::umd
