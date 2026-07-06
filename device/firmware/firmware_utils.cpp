// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_utils.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_set>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
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
