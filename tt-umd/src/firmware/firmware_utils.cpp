// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_utils.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"

namespace tt::umd {

FirmwareBundleVersion get_minimum_compatible_firmware_version(tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

FirmwareBundleVersion get_latest_supported_firmware_version(tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data) { UMD_NOT_IMPLEMENTED(); }

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) { UMD_NOT_IMPLEMENTED(); }

SemVer get_eth_fw_version(TTDevice* tt_device, CoreCoord eth_core) { UMD_NOT_IMPLEMENTED(); }

void set_tdp_limit(TTDevice* tt_device, uint32_t tdp_limit_watts) { UMD_NOT_IMPLEMENTED(); }

void restore_default_tdp_limit(TTDevice* tt_device) { UMD_NOT_IMPLEMENTED(); }

std::vector<std::pair<CoreCoord, bool>> filter_harvested_eth_status(
    const std::vector<std::pair<CoreCoord, bool>>& statuses, const SocDescriptor& soc_desc) {
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
