// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/semver.hpp"

namespace tt::umd {
FirmwareBundleVersion get_firmware_version_util(TTDevice* tt_device);

SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data);

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_eth_fw_version(TTDevice* tt_device, tt_xy_pair eth_core);

}  // namespace tt::umd
