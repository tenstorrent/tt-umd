// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/firmware/firmware_utils.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/unsupported.hpp"
#include "tt-umd/arch/blackhole_implementation.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/types/blackhole_arc.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/utils/timeouts.hpp"

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
            return FirmwareBundleVersion(19, 14, 0);
        default:
            UMD_NOT_SUPPORTED("Firmware versioning of " + arch_to_str(arch));
    }
}

namespace {

// Bundle-style version word: major, minor, patch and pre-release, one byte each from the top.
SemVer parse_bundle_style_version(uint32_t version) {
    return SemVer((version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
}

void require_blackhole(tt::ARCH arch, const char* what) {
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_NOT_SUPPORTED(std::string(what) + " of " + arch_to_str(arch));
    }
}

// Throws unless the device firmware takes SET_TDP_LIMIT requests.
void require_tdp_limit_control(TTDevice* tt_device) {
    const FirmwareBundleVersion minimum_version(19, 11, 0);
    if (tt_device->get_arch() != tt::ARCH::BLACKHOLE || tt_device->get_firmware_version() < minimum_version) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Setting the TDP limit needs a Blackhole with firmware {} or newer; device {} is a {} with firmware "
                "{}.",
                minimum_version.to_string(),
                tt_device->get_communication_device_id(),
                arch_to_str(tt_device->get_arch()),
                tt_device->get_firmware_version().to_string()));
    }
}

void send_set_tdp_limit(TTDevice* tt_device, uint32_t tdp_limit_watts, bool restore_default) {
    const DeviceCommandResult result = tt_device->get_device_firmware()->send_device_command(
        static_cast<uint32_t>(blackhole::ArcMessageType::SET_TDP_LIMIT),
        {tdp_limit_watts, restore_default ? 1u : 0u},
        timeout::ARC_MESSAGE_TIMEOUT,
        get_selected_noc_id());
    if (result.exit_code != 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Device {} firmware refused to {} the TDP limit (status {:#x}).",
                tt_device->get_communication_device_id(),
                restore_default ? "restore" : fmt::format("set {} W as", tdp_limit_watts),
                result.exit_code));
    }
}

}  // namespace

// tt-flash records its version as major, minor and patch in the low three bytes.
SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data) {
    return SemVer((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    require_blackhole(arch, "CM firmware version");
    return parse_bundle_style_version(telemetry_data);
}

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    require_blackhole(arch, "DM application firmware version");
    return parse_bundle_style_version(telemetry_data);
}

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    require_blackhole(arch, "DM bootloader firmware version");
    return parse_bundle_style_version(telemetry_data);
}

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    require_blackhole(arch, "GDDR firmware version");
    // Laid out like the Blackhole ETH firmware tag: 16-bit major, then 8-bit minor and patch.
    return SemVer::from_blackhole_eth_firmware_tag(telemetry_data);
}

SemVer get_eth_fw_version(TTDevice* tt_device, CoreCoord eth_core) {
    require_blackhole(tt_device->get_arch(), "ETH firmware version");
    // The ETH firmware keeps its version bytes in one aligned word of its L1 boot results.
    uint32_t version_word = 0;
    tt_device->read_from_device(&version_word, eth_core, blackhole::ETH_FW_PATCH_ADDR, sizeof(version_word));
    const auto version_byte = [&](uint64_t addr) {
        return (version_word >> (8 * (addr - blackhole::ETH_FW_PATCH_ADDR))) & 0xFF;
    };
    return SemVer(
        version_byte(blackhole::ETH_FW_MAJOR_ADDR),
        version_byte(blackhole::ETH_FW_MINOR_ADDR),
        version_byte(blackhole::ETH_FW_PATCH_ADDR));
}

void set_tdp_limit(TTDevice* tt_device, uint32_t tdp_limit_watts) {
    require_tdp_limit_control(tt_device);
    // The firmware's TDP throttler works within this range.
    constexpr uint32_t MIN_TDP_LIMIT_WATTS = 50;
    constexpr uint32_t MAX_TDP_LIMIT_WATTS = 500;
    if (tdp_limit_watts < MIN_TDP_LIMIT_WATTS || tdp_limit_watts > MAX_TDP_LIMIT_WATTS) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "TDP limit {} W is outside [{}, {}] W.", tdp_limit_watts, MIN_TDP_LIMIT_WATTS, MAX_TDP_LIMIT_WATTS));
    }
    send_set_tdp_limit(tt_device, tdp_limit_watts, false);
}

void restore_default_tdp_limit(TTDevice* tt_device) {
    require_tdp_limit_control(tt_device);
    send_set_tdp_limit(tt_device, 0, true);
}

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
