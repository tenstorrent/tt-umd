/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "api/umd/device/tt_device/tt_device.hpp"

namespace tt::umd {

class WarmReset {
public:
    static void warm_reset(bool reset_m3 = false);

private:
    static constexpr int POST_RESET_WAIT = 2;

    static void warm_reset_blackhole();

    static void warm_reset_wormhole(bool reset_m3);
};

}  // namespace tt::umd
