/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "umd/device/utils/semver.hpp"

namespace tt::umd::erisc_firmware {

// ERISC FW versions required by UMD.
constexpr SemVer BH_MIN_ERISC_FW_SUPPORTED_VERSION = SemVer(1, 4, 1);
constexpr SemVer WH_MIN_ERISC_FW_SUPPORTED_VERSION = SemVer(6, 14, 0);

constexpr uint32_t BASE_FW_HEARTBEAT_SIGNATURE = 0xABCD;
constexpr uint32_t METAL_FW_HEARTBEAT_SIGNATURE = 0xAABB;
constexpr uint32_t FABRIC_HEARTBEAT_SIGNATURE = 0xDCBA;

}  // namespace tt::umd::erisc_firmware
