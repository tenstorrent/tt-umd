// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_utils.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_eth.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

FirmwareBundleVersion get_firmware_version_util(TTDevice* tt_device) {
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        SmBusArcTelemetryReader smbus_reader(tt_device);
        return FirmwareBundleVersion::from_firmware_bundle_tag(
            smbus_reader.read_entry(wormhole::LegacyTelemetryTag::FW_BUNDLE_VERSION));
    }
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)
               ? FirmwareBundleVersion::from_firmware_bundle_tag(
                     telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION))
               : FirmwareBundleVersion(0, 0, 0);
}

SemVer get_tt_flash_version_from_telemetry(const uint32_t telemetry_data) {
    return SemVer((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

SemVer get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return SemVer((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
    }

    return SemVer((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

SemVer get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return SemVer((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
    }

    return SemVer((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

SemVer get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return SemVer(0, 0, 0);
    }

    return SemVer((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

SemVer get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return SemVer((telemetry_data >> 16) & 0xFFFF, telemetry_data & 0xFFFF, 0);
    }

    return SemVer(0, 0, 0);
}

SemVer get_eth_fw_version(TTDevice* tt_device, CoreCoord eth_core) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0: {
            uint32_t eth_fw_version_read;
            tt_device->read_from_device(
                &eth_fw_version_read, eth_core, wormhole::ETH_FW_VERSION_ADDR, sizeof(uint32_t));
            return SemVer::from_wormhole_eth_firmware_tag(eth_fw_version_read);
        }
        case ARCH::BLACKHOLE: {
            uint8_t major = 0;
            uint8_t minor = 0;
            uint8_t patch = 0;
            tt_device->read_from_device(&major, eth_core, blackhole::ETH_FW_MAJOR_ADDR, sizeof(uint8_t));
            tt_device->read_from_device(&minor, eth_core, blackhole::ETH_FW_MINOR_ADDR, sizeof(uint8_t));
            tt_device->read_from_device(&patch, eth_core, blackhole::ETH_FW_PATCH_ADDR, sizeof(uint8_t));
            return SemVer(major, minor, patch);
        }
        default:
            UMD_THROW(error::RuntimeError, "Getting ETH FW version is not supported for this device.");
    }
}

// Range the Blackhole TDP throttler accepts, mirroring throttler_limit_ranges[kThrottlerTDP] in CMFW.
constexpr uint32_t TDP_LIMIT_MIN_WATTS = 50;
constexpr uint32_t TDP_LIMIT_MAX_WATTS = 500;

// CMFW release that introduced ArcMessageType::SET_TDP_LIMIT.
const FirmwareBundleVersion TDP_LIMIT_MIN_FIRMWARE_VERSION(19, 8, 0);

static void verify_tdp_limit_supported(TTDevice* tt_device) {
    tt::ARCH arch = tt_device->get_arch();
    if (arch != tt::ARCH::BLACKHOLE) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Changing the TDP limit is supported only on Blackhole, but this device is {}.", arch));
    }

    FirmwareBundleVersion fw_version = tt_device->get_firmware_info_provider()->get_firmware_version();
    if (fw_version < TDP_LIMIT_MIN_FIRMWARE_VERSION) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Changing the TDP limit requires firmware {} or newer, but this device runs {}.",
                TDP_LIMIT_MIN_FIRMWARE_VERSION.to_string(),
                fw_version.to_string()));
    }
}

// send_message drops the firmware exit code that signals refusal, so the outcome is confirmed by reading
// the limit back: firmware only rewrites it once it has accepted a new one.
static uint32_t apply_tdp_limit(TTDevice* tt_device, const uint32_t tdp_limit_watts, const bool restore_default) {
    verify_tdp_limit_supported(tt_device);

    tt_device->get_arc_messenger()->send_message(
        static_cast<uint32_t>(blackhole::ArcMessageType::SET_TDP_LIMIT),
        {restore_default ? 0u : tdp_limit_watts, restore_default ? 1u : 0u});

    std::optional<uint32_t> applied_limit = tt_device->get_firmware_info_provider()->get_tdp_limit();
    if (!applied_limit.has_value()) {
        UMD_THROW(
            error::RuntimeError,
            "Firmware does not report the TDP limit through telemetry, so the requested change cannot be confirmed.");
    }
    return applied_limit.value();
}

void set_tdp_limit(TTDevice* tt_device, const uint32_t tdp_limit_watts) {
    if (tdp_limit_watts < TDP_LIMIT_MIN_WATTS || tdp_limit_watts > TDP_LIMIT_MAX_WATTS) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "TDP limit of {} W is outside the [{}, {}] W range that firmware accepts.",
                tdp_limit_watts,
                TDP_LIMIT_MIN_WATTS,
                TDP_LIMIT_MAX_WATTS));
    }

    const uint32_t applied_limit = apply_tdp_limit(tt_device, tdp_limit_watts, false);
    if (applied_limit != tdp_limit_watts) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Firmware refused a TDP limit of {} W and is still enforcing {} W. A limit above max_tdp_limit in "
                "the board's SPI firmware table is refused, and that field is only populated on Blackhole Galaxy "
                "boards, so on other boards the limit cannot be raised past the board default.",
                tdp_limit_watts,
                applied_limit));
    }

    log_debug(tt::LogUMD, "TDP limit set to {} W.", applied_limit);
}

uint32_t restore_default_tdp_limit(TTDevice* tt_device) {
    const uint32_t applied_limit = apply_tdp_limit(tt_device, 0, true);
    log_debug(tt::LogUMD, "TDP limit restored to the board default of {} W.", applied_limit);
    return applied_limit;
}

std::vector<std::pair<CoreCoord, bool>> filter_harvested_eth_status(
    const std::vector<std::pair<CoreCoord, bool>>& statuses, const SocDescriptor& soc_desc) {
    auto harvested_cores = soc_desc.get_harvested_cores(CoreType::ETH, CoordSystem::NOC0);
    std::unordered_set<CoreCoord> harvested(harvested_cores.begin(), harvested_cores.end());

    std::vector<std::pair<CoreCoord, bool>> filtered;
    filtered.reserve(statuses.size());
    std::copy_if(statuses.begin(), statuses.end(), std::back_inserter(filtered), [&](const auto& entry) {
        return harvested.count(entry.first) == 0;
    });
    return filtered;
}

}  // namespace tt::umd
