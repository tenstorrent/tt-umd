// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {
SemVer get_firmware_version_util(TTDevice* tt_device);

std::optional<SemVer> get_expected_eth_firmware_version_from_firmware_bundle(SemVer fw_bundle_version, tt::ARCH arch);

SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data);

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

std::optional<bool> verify_eth_fw_integrity(TTDevice* tt_device, tt_xy_pair eth_core, SemVer eth_fw_version);

}  // namespace tt::umd
