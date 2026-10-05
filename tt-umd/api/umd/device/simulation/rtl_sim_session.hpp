// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Backward-compat forwarding header: kept so existing consumers that still
// #include "umd/device/simulation/rtl_sim_session.hpp" keep building after the tt-umd naming rename
// (see #2751). New code should include "tt-umd/simulation/rtl_sim_session.hpp" directly.
#include "tt-umd/simulation/rtl_sim_session.hpp"
