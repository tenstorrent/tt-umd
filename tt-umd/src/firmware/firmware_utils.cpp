// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_utils.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/blackhole_arc.hpp"
#include "tt-umd/types/wormhole_eth.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt::umd {

namespace {

// Firmware publishes most component versions as 0xAABBCCDD: major, minor, patch, then a build byte.
SemVer from_major_minor_patch_byte_tag(uint32_t telemetry_data) {
    return SemVer((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
}

// TDP limits the firmware throttler accepts [W].
constexpr uint32_t MIN_TDP_LIMIT_WATTS = 50;
constexpr uint32_t MAX_TDP_LIMIT_WATTS = 500;
constexpr uint32_t RESTORE_DEFAULT_TDP_LIMIT = 1;

// First firmware bundle whose SET_TDP_LIMIT handling UMD supports.
const FirmwareBundleVersion MIN_TDP_LIMIT_FIRMWARE_VERSION(19, 11, 0);

void send_tdp_limit_command(TTDevice* tt_device, uint32_t tdp_limit_watts, uint32_t flags) {
    UMD_ASSERT(
        tt_device->get_arch() == tt::ARCH::BLACKHOLE,
        error::RuntimeError,
        fmt::format("Setting the TDP limit is not supported on {}.", arch_to_str(tt_device->get_arch())));

    const FirmwareBundleVersion firmware_version = tt_device->get_firmware_version();
    UMD_ASSERT(
        firmware_version >= MIN_TDP_LIMIT_FIRMWARE_VERSION,
        error::RuntimeError,
        fmt::format(
            "Setting the TDP limit needs firmware {} or newer, the device runs {}.",
            MIN_TDP_LIMIT_FIRMWARE_VERSION.to_string(),
            firmware_version.to_string()));

    const DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
        static_cast<uint32_t>(blackhole::ArcMessageType::SET_TDP_LIMIT),
        {tdp_limit_watts, flags},
        timeout::ARC_MESSAGE_TIMEOUT);
    UMD_ASSERT(
        result.exit_code == 0,
        error::RuntimeError,
        fmt::format("Firmware refused the TDP limit request with status {}.", result.exit_code));
}

}  // namespace

FirmwareBundleVersion get_minimum_compatible_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return FirmwareBundleVersion(18, 3, 0);
        case tt::ARCH::BLACKHOLE:
            return FirmwareBundleVersion(18, 5, 0);
        default:
            return FirmwareBundleVersion(0, 0, 0);
    }
}

FirmwareBundleVersion get_latest_supported_firmware_version(tt::ARCH arch) {
    switch (arch) {
        case tt::ARCH::WORMHOLE_B0:
            return FirmwareBundleVersion(18, 12, 0);
        case tt::ARCH::BLACKHOLE:
            return FirmwareBundleVersion(19, 17, 1);
        default:
            return FirmwareBundleVersion(0, 0, 0);
    }
}

SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data) {
    return SemVer((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH /*arch*/) {
    return from_major_minor_patch_byte_tag(telemetry_data);
}

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH /*arch*/) {
    return from_major_minor_patch_byte_tag(telemetry_data);
}

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH /*arch*/) {
    return from_major_minor_patch_byte_tag(telemetry_data);
}

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH /*arch*/) {
    // GDDR firmware versions have no patch level: (major << 16) | minor.
    return SemVer((telemetry_data >> 16) & 0xFFFF, telemetry_data & 0xFFFF, 0);
}

SemVer get_eth_fw_version(TTDevice* tt_device, CoreCoord eth_core) {
    uint32_t version_tag = 0;
    switch (tt_device->get_arch()) {
        case tt::ARCH::WORMHOLE_B0:
            tt_device->read_from_device(&version_tag, eth_core, wormhole::ETH_FW_VERSION_ADDR, sizeof(version_tag));
            return SemVer::from_wormhole_eth_firmware_tag(version_tag);
        case tt::ARCH::BLACKHOLE:
            // The ERISC firmware keeps its version as patch, minor and major bytes from this address up.
            tt_device->read_from_device(&version_tag, eth_core, blackhole::ETH_FW_PATCH_ADDR, sizeof(version_tag));
            return SemVer::from_blackhole_eth_firmware_tag(version_tag);
        default:
            UMD_THROW(
                error::RuntimeError,
                fmt::format("ETH firmware version is not available on {}.", arch_to_str(tt_device->get_arch())));
    }
}

void set_tdp_limit(TTDevice* tt_device, uint32_t tdp_limit_watts) {
    UMD_ASSERT(
        tdp_limit_watts >= MIN_TDP_LIMIT_WATTS && tdp_limit_watts <= MAX_TDP_LIMIT_WATTS,
        error::RuntimeError,
        fmt::format(
            "TDP limit {} W is outside [{}, {}] W.", tdp_limit_watts, MIN_TDP_LIMIT_WATTS, MAX_TDP_LIMIT_WATTS));
    send_tdp_limit_command(tt_device, tdp_limit_watts, 0);
}

void restore_default_tdp_limit(TTDevice* tt_device) { send_tdp_limit_command(tt_device, 0, RESTORE_DEFAULT_TDP_LIMIT); }

std::vector<std::pair<CoreCoord, bool>> filter_harvested_eth_status(
    const std::vector<std::pair<CoreCoord, bool>>& statuses, const SocDescriptor& soc_desc) {
    std::set<tt_xy_pair> harvested_cores;
    for (const CoreCoord& core : soc_desc.get_harvested_cores(CoreType::ETH, CoordSystem::NOC0)) {
        harvested_cores.insert(core.to_pair());
    }

    std::vector<std::pair<CoreCoord, bool>> filtered;
    for (const auto& [core, status] : statuses) {
        const tt_xy_pair noc0_core = core.coord_system == CoordSystem::NOC0
                                         ? core.to_pair()
                                         : soc_desc.translate_coord_to(core, CoordSystem::NOC0).to_pair();
        if (harvested_cores.count(noc0_core) == 0) {
            filtered.emplace_back(core, status);
        }
    }
    return filtered;
}

}  // namespace tt::umd
