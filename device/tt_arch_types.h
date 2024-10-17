/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

// defines of tt_arch_types
#include "device/architecture.h"

namespace tt {

// TODO: why do we have ARCH and architecture?  This is a mess.  Can we have just one?
// Can we get rid of the entries that (for all practical purposes) do not exist?
/**
 * @brief ARCH Enums
 */
enum class ARCH {
    JAWBRIDGE = static_cast<int>(tt::umd::architecture::jawbridge),
    GRAYSKULL = static_cast<int>(tt::umd::architecture::grayskull),
    WORMHOLE = static_cast<int>(tt::umd::architecture::wormhole),
    WORMHOLE_B0 = static_cast<int>(tt::umd::architecture::wormhole_b0),
    BLACKHOLE = static_cast<int>(tt::umd::architecture::blackhole),
    Invalid = static_cast<int>(tt::umd::architecture::invalid),
};
}
