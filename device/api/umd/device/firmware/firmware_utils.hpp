// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {
static semver_t fw_version_from_telemetry(const uint32_t telemetry_data);

semver_t get_firmware_version_util(TTDevice* tt_device);

semver_t get_eth_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

semver_t get_tt_flash_version_from_telemetry(const uint32_t telemetry_data);

semver_t get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

semver_t get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

semver_t get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

semver_t get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

}  // namespace tt::umd
