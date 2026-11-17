// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_utils.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"
#include "tt-umd/soc_descriptor.hpp"

namespace tt::umd {

FirmwareBundleVersion get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return FirmwareBundleVersion(18, 3, 0);
        case tt::ARCH::BLACKHOLE:
            return FirmwareBundleVersion(18, 5, 0);
        default:
            UMD_NOT_SUPPORTED("Firmware versioning of " + arch_to_str(arch));
    }
}

FirmwareBundleVersion get_latest_supported_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
        case tt::ARCH::BLACKHOLE:
            return FirmwareBundleVersion(19, 11, 0);
        default:
            UMD_NOT_SUPPORTED("Firmware versioning of " + arch_to_str(arch));
    }
}

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
    const std::vector<CoreCoord> harvested_eth_cores = soc_desc.get_harvested_cores(CoreType::ETH, CoordSystem::NOC0);
    std::vector<std::pair<CoreCoord, bool>> filtered;
    for (const auto& [eth_core, status] : statuses) {
        const CoreCoord noc0_core = soc_desc.translate_coord_to(eth_core, CoordSystem::NOC0);
        if (std::find(harvested_eth_cores.begin(), harvested_eth_cores.end(), noc0_core) == harvested_eth_cores.end()) {
            filtered.emplace_back(eth_core, status);
        }
    }
    return filtered;
}

}  // namespace tt::umd
