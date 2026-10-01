// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Backward-compat forwarding header: kept so existing consumers that still
// #include "umd/device/firmware/firmware_info_provider_implementation.hpp" keep building after the tt-umd naming rename
// (see #2751). New code should include "tt-umd/firmware/firmware_info_provider_implementation.hpp" directly.
#include "tt-umd/firmware/firmware_info_provider_implementation.hpp"
