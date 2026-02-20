// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/firmware/firmware_utils.hpp"

#include <picosha2.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

FirmwareBundleVersion get_firmware_version_util(TTDevice* tt_device) {
    if (tt_device->get_arch() == tt::ARCH::WORMHOLE_B0) {
        std::unique_ptr<SmBusArcTelemetryReader> smbus_telemetry_reader =
            std::make_unique<SmBusArcTelemetryReader>(tt_device);

        // Poll for a valid firmware version. If no valid version is found within 250ms,
        // log a warning and return the last read value.
        auto start = std::chrono::steady_clock::now();
        auto timeout_duration = std::chrono::milliseconds(250);
        while (std::chrono::steady_clock::now() - start < timeout_duration) {
            auto fw_bundle_version = smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::FW_BUNDLE_VERSION);
            if (fw_bundle_version != 0) {
                return FirmwareBundleVersion::from_firmware_bundle_tag(fw_bundle_version);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        log_warning(
            tt::LogUMD, "Timeout reading firmware bundle version (250ms), returning potentially invalid version");
        return FirmwareBundleVersion::from_firmware_bundle_tag(
            smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::FW_BUNDLE_VERSION));
    }
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)
               ? FirmwareBundleVersion::from_firmware_bundle_tag(
                     telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION))
               : FirmwareBundleVersion(0, 0, 0);
}

std::optional<SemVer> get_expected_eth_firmware_version_from_firmware_bundle(
    FirmwareBundleVersion fw_bundle_version, tt::ARCH arch) {
    // Skip checks for pre-release firmware bundles.
    if (fw_bundle_version.pre_release != 0) {
        return std::nullopt;
    }

    const auto* version_map = &erisc_firmware::WH_ERISC_FW_VERSION_MAP;
    switch (arch) {
        case ARCH::WORMHOLE_B0:
            version_map = &erisc_firmware::WH_ERISC_FW_VERSION_MAP;
            break;
        case ARCH::BLACKHOLE:
            version_map = &erisc_firmware::BH_ERISC_FW_VERSION_MAP;
            break;
        default:
            return std::nullopt;
    }

    // Find the most recently updated ERISC FW version from a given firmware
    // bundle version.
    for (auto it = version_map->cbegin(); it != version_map->cend(); ++it) {
        if (it->first > fw_bundle_version) {
            if (it == version_map->cbegin()) {
                return std::nullopt;
            } else {
                return std::prev(it)->second;
            }
        }
    }
    return version_map->back().second;
}

std::optional<bool> verify_eth_fw_integrity(TTDevice* tt_device, tt_xy_pair eth_core, SemVer eth_fw_version) {
    const std::unordered_map<SemVer, erisc_firmware::HashedAddressRange>* eth_fw_hashes = nullptr;
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0:
            eth_fw_hashes = &erisc_firmware::WH_ERISC_FW_HASHES;
            break;
        case ARCH::BLACKHOLE:
            eth_fw_hashes = &erisc_firmware::BH_ERISC_FW_HASHES;
            break;
        default:
            return std::nullopt;
    }

    if (eth_fw_hashes->find(eth_fw_version) == eth_fw_hashes->end()) {
        return std::nullopt;
    }

    erisc_firmware::HashedAddressRange hashed_range = eth_fw_hashes->at(eth_fw_version);
    std::vector<uint8_t> eth_fw_text(hashed_range.size);
    tt_device->read_from_device(eth_fw_text.data(), eth_core, hashed_range.start_address, hashed_range.size);
    std::string eth_fw_text_sha256_hash = picosha2::hash256_hex_string(eth_fw_text);

    return eth_fw_text_sha256_hash == hashed_range.sha256_hash;
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

}  // namespace tt::umd
