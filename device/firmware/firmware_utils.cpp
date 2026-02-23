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
#include <stdexcept>
#include <string>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

semver_t get_firmware_version_util(TTDevice* tt_device) {
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
                return semver_t::from_firmware_bundle_tag(fw_bundle_version);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        log_warning(
            tt::LogUMD, "Timeout reading firmware bundle version (250ms), returning potentially invalid version");
        return semver_t::from_firmware_bundle_tag(
            smbus_telemetry_reader->read_entry(wormhole::TelemetryTag::FW_BUNDLE_VERSION));
    }
    ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();
    return telemetry->is_entry_available(TelemetryTag::FLASH_BUNDLE_VERSION)
               ? semver_t::from_firmware_bundle_tag(telemetry->read_entry(TelemetryTag::FLASH_BUNDLE_VERSION))
               : semver_t(0, 0, 0);
}

std::optional<semver_t> get_expected_eth_firmware_version_from_firmware_bundle(
    semver_t fw_bundle_version, tt::ARCH arch) {
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
        if (semver_t::compare_firmware_bundle(it->first, fw_bundle_version) > 0) {
            if (it == version_map->cbegin()) {
                return std::nullopt;
            } else {
                return std::prev(it)->second;
            }
        }
    }
    return version_map->back().second;
}

std::optional<bool> verify_eth_fw_integrity(TTDevice* tt_device, tt_xy_pair eth_core, semver_t eth_fw_version) {
    const std::unordered_map<semver_t, erisc_firmware::HashedAddressRange>* eth_fw_hashes = nullptr;
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

semver_t get_tt_flash_version_from_telemetry(const uint32_t telemetry_data) {
    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_cm_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_dm_app_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t((telemetry_data >> 24) & 0xFF, (telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_dm_bl_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t(0, 0, 0);
    }

    return semver_t((telemetry_data >> 16) & 0xFF, (telemetry_data >> 8) & 0xFF, telemetry_data & 0xFF);
}

semver_t get_gddr_fw_version_from_telemetry(const uint32_t telemetry_data, tt::ARCH arch) {
    if (arch == tt::ARCH::BLACKHOLE) {
        return semver_t((telemetry_data >> 16) & 0xFFFF, telemetry_data & 0xFFFF, 0);
    }

    return semver_t(0, 0, 0);
}

semver_t get_eth_fw_version(TTDevice* tt_device, tt_xy_pair eth_core) {
    switch (tt_device->get_arch()) {
        case ARCH::WORMHOLE_B0: {
            uint32_t eth_fw_version_read;
            tt_device->read_from_device(
                &eth_fw_version_read, eth_core, wormhole::ETH_FW_VERSION_ADDR, sizeof(uint32_t));
            return semver_t::from_wormhole_eth_firmware_tag(eth_fw_version_read);
        }
        case ARCH::BLACKHOLE: {
            uint8_t major = 0;
            uint8_t minor = 0;
            uint8_t patch = 0;
            tt_device->read_from_device(&major, eth_core, blackhole::ETH_FW_MAJOR_ADDR, sizeof(uint8_t));
            tt_device->read_from_device(&minor, eth_core, blackhole::ETH_FW_MINOR_ADDR, sizeof(uint8_t));
            tt_device->read_from_device(&patch, eth_core, blackhole::ETH_FW_PATCH_ADDR, sizeof(uint8_t));
            return semver_t(major, minor, patch);
        }
        default:
            throw std::runtime_error("Getting ETH FW version is not supported for this device.");
    }
}

}  // namespace tt::umd
