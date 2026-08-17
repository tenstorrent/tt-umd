// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {
class TTDevice;
class SocDescriptor;

FirmwareBundleVersion get_minimum_compatible_firmware_version(tt::ARCH arch);

FirmwareBundleVersion get_latest_supported_firmware_version(tt::ARCH arch);

FirmwareBundleVersion get_firmware_version_util(TTDevice* tt_device);

SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data);

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch);

SemVer get_eth_fw_version(TTDevice* tt_device, CoreCoord eth_core);

/**
 * Set the TDP limit the firmware throttler enforces [W]. Blackhole with firmware 19.11.0+ only.
 * Applies per ASIC, is lost on chip reset, and is not arbitrated between callers: last write wins.
 * Firmware refuses limits above the board's max_tdp_limit, which only Galaxy boards populate, so
 * elsewhere the limit can only be lowered. Throws if unsupported, out of [50, 500] W, or refused.
 */
void set_tdp_limit(TTDevice* tt_device, uint32_t tdp_limit_watts);

/**
 * Restore the TDP limit to the board default from the SPI firmware table.
 * Read the restored value back with FirmwareInfoProvider::get_tdp_limit().
 */
void restore_default_tdp_limit(TTDevice* tt_device);

/**
 * Filter an ETH status vector to only include non-harvested cores.
 * Uses the SocDescriptor's harvesting information to remove entries
 * for ETH cores that have been harvested.
 */
std::vector<std::pair<CoreCoord, bool>> filter_harvested_eth_status(
    const std::vector<std::pair<CoreCoord, bool>>& statuses, const SocDescriptor& soc_desc);

}  // namespace tt::umd
