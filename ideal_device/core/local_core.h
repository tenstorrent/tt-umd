/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "core.h"

namespace tt::umd {

// This is a layer which should be used by a regular user.
// This hides implementation details for local, remote, versim, and mock cores.
class LocalCore: public Core {

    // asserts the right type of core for l1 and dram membars
    // for dram membar using channels, the socdescriptor should give you the right cores for it. MAYBE we can have run_on_dram_channels inside chip.h
    void l1_membar();
    void dram_membar();
};

}