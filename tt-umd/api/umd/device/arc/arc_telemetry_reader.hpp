// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Backward-compat forwarding header: kept so existing consumers that still
// #include "umd/device/arc/arc_telemetry_reader.hpp" keep building after the tt-umd naming rename
// (see #2751). New code should include "tt-umd/arc/arc_telemetry_reader.hpp" directly.
#include "tt-umd/arc/arc_telemetry_reader.hpp"
