// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "umd/device/firmware/firmware_telemetry_mapping.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/gddr_telemetry.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

/**
 * @defgroup tt_firmware_info_provider FirmwareInfoProvider
 * @{
 *
 * @brief Unified interface for firmware metadata and device telemetry.
 *
 * Provides a stable interface to firmware metadata and device telemetry
 * regardless of the firmware version running on the device.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref FirmwareTelemetryReader | Low-level reader for raw telemetry values |
 * | @ref SemVer | Semantic version (major.minor.patch) |
 * | @ref DramTrainingStatus | Per-channel DRAM training result |
 * | @ref GddrTelemetry | Aggregated GDDR telemetry across all modules |
 * | @ref GddrModuleTelemetry | Per-module GDDR telemetry |
 * | @ref GddrModule | GDDR module identifier |
 *
 */

class FirmwareInfoProvider final {
public:
    /**
     * @brief Creates a FirmwareInfoProvider for the given telemetry reader and architecture.
     *
     * Selects the appropriate feature-to-telemetry mapping table based on the
     * architecture and the firmware version read from the telemetry reader.
     *
     * @param telemetry_reader Non-owning pointer to the device's telemetry reader.
     * @param arch Device architecture.
     * @return std::unique_ptr<FirmwareInfoProvider> A configured info provider instance.
     */
    static std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider(
        FirmwareTelemetryReader* telemetry_reader, ARCH arch);

    /**
     * @brief Constructs a FirmwareInfoProvider.
     *
     * @param telemetry_reader Non-owning pointer to the device's telemetry reader.
     * @param arch Device architecture.
     */
    FirmwareInfoProvider(FirmwareTelemetryReader* telemetry_reader, ARCH arch);

    ~FirmwareInfoProvider() = default;

    /**
     * @brief Retrieves the firmware bundle version running on the device.
     * @return FirmwareBundleVersion The active firmware version.
     */
    FirmwareBundleVersion get_firmware_version() const;

    /**
     * @brief Returns the minimum firmware version compatible with this UMD build.
     * @param arch Target architecture.
     * @return FirmwareBundleVersion Minimum compatible version.
     */
    static FirmwareBundleVersion get_minimum_compatible_firmware_version(tt::ARCH arch);

    /**
     * @brief Returns the latest firmware version supported by this UMD build.
     *
     * Updated on every firmware release to track supported features.
     *
     * @param arch Target architecture.
     * @return FirmwareBundleVersion Latest supported version.
     */
    static FirmwareBundleVersion get_latest_supported_firmware_version(tt::ARCH arch);

    /**
     * @brief Retrieves the unique board identifier.
     * @return std::optional<uint64_t> Board ID, or std::nullopt if unavailable.
     */
    std::optional<uint64_t> get_board_id() const;

    /**
     * @brief Retrieves the physical slot index of this chip on a multi-chip board.
     * @return std::optional<uint8_t> ASIC location index, or std::nullopt if unavailable.
     */
    std::optional<uint8_t> get_asic_location() const;

    /**
     * @brief Retrieves the Ethernet firmware version as a raw tag value.
     * @return std::optional<uint32_t> Raw version tag, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_eth_fw_version() const;

    /**
     * @brief Retrieves the Ethernet firmware version as a semantic version.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    std::optional<SemVer> get_eth_fw_version_semver() const;

    /**
     * @brief Retrieves the GDDR firmware version.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    std::optional<SemVer> get_gddr_fw_version() const;

    /**
     * @brief Retrieves the CM (Clock Manager) firmware version.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    std::optional<SemVer> get_cm_fw_version() const;

    /**
     * @brief Retrieves the DM application firmware version.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    std::optional<SemVer> get_dm_app_fw_version() const;

    /**
     * @brief Retrieves the DM bootloader firmware version.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    std::optional<SemVer> get_dm_bl_fw_version() const;

    /**
     * @brief Retrieves the TT-Flash version.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    std::optional<SemVer> get_tt_flash_version() const;

    /**
     * @brief Retrieves the ASIC temperature.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    std::optional<double> get_asic_temperature() const;

    /**
     * @brief Retrieves the board temperature.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    std::optional<double> get_board_temperature() const;

    /**
     * @brief Retrieves the thermal shutdown threshold.
     * @return std::optional<double> Threshold in degrees Celsius, or std::nullopt if unavailable.
     */
    std::optional<double> get_thm_limit_shutdown() const;

    /**
     * @brief Retrieves the thermal throttle threshold.
     * @return std::optional<double> Threshold in degrees Celsius, or std::nullopt if unavailable.
     */
    std::optional<double> get_thm_limit_throttle() const;

    /**
     * @brief Retrieves the thermal trip count.
     * @return std::optional<uint32_t> Number of thermal trips, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_therm_trip_count() const;

    /**
     * @brief Retrieves the current AICLK frequency.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_aiclk() const;

    /**
     * @brief Retrieves the current AXICLK frequency.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_axiclk() const;

    /**
     * @brief Retrieves the current ARCCLK frequency.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_arcclk() const;

    /**
     * @brief Retrieves the maximum supported clock frequency.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_max_clock_freq() const;

    /**
     * @brief Retrieves the Thermal Design Power.
     * @return std::optional<uint32_t> TDP in watts, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_tdp() const;

    /**
     * @brief Retrieves the Thermal Design Current.
     * @return std::optional<uint32_t> TDC in amps, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_tdc() const;

    /**
     * @brief Retrieves the core voltage.
     * @return std::optional<uint32_t> VCORE in mV, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_vcore() const;

    /**
     * @brief Retrieves the board power limit.
     * @return std::optional<uint32_t> Power limit in watts, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_board_power_limit() const;

    /**
     * @brief Retrieves the fan speed as a percentage (0-100).
     * @return std::optional<uint32_t> Fan speed in percent, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_fan_speed() const;

    /**
     * @brief Retrieves the fan speed in RPM.
     * @return std::optional<uint32_t> Fan speed in RPM, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_fan_rpm() const;

    /**
     * @brief Retrieves per-link Ethernet heartbeat status.
     *
     * Vector indices align with ETH channels (logical coordinates, up to 16).
     *
     * @return std::optional<std::vector<bool>> Per-link status (true = active), or std::nullopt if unavailable.
     */
    std::optional<std::vector<bool>> get_eth_heartbeat_status() const;

    /**
     * @brief Retrieves per-link Ethernet retrain status.
     *
     * Vector indices align with ETH channels (logical coordinates, up to 16).
     *
     * @return std::optional<std::vector<bool>> Per-link status (true = retrained), or std::nullopt if unavailable.
     */
    std::optional<std::vector<bool>> get_eth_retrain_status() const;

    /**
     * @brief Retrieves per-channel DRAM training status.
     * @param num_dram_channels Number of DRAM channels to query.
     * @return std::vector<DramTrainingStatus> Per-channel training status. Empty if unavailable.
     */
    std::vector<DramTrainingStatus> get_dram_training_status(uint32_t num_dram_channels) const;

    /**
     * @brief Retrieves aggregated telemetry across all GDDR modules.
     * @return std::optional<GddrTelemetry> Aggregated DRAM telemetry, or std::nullopt if unavailable.
     */
    std::optional<GddrTelemetry> get_aggregated_dram_telemetry() const;

    /**
     * @brief Retrieves telemetry for a specific GDDR module.
     * @param gddr_module The target GDDR module.
     * @return std::optional<GddrModuleTelemetry> Module telemetry, or std::nullopt if unavailable.
     */
    std::optional<GddrModuleTelemetry> get_dram_telemetry(GddrModule gddr_module) const;

    /**
     * @brief Retrieves the GDDR speed.
     * @return std::optional<uint16_t> Speed value, or std::nullopt if unavailable.
     */
    std::optional<uint16_t> get_dram_speed() const;

    /**
     * @brief Retrieves the current maximum DRAM temperature across all modules.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    std::optional<double> get_current_max_dram_temperature() const;

    /**
     * @brief Retrieves the firmware heartbeat counter.
     *
     * A monotonically non-decreasing value that indicates the firmware is alive.
     *
     * @return std::optional<uint32_t> Heartbeat counter, or std::nullopt if unavailable.
     */
    std::optional<uint32_t> get_heartbeat() const;

private:
    /**
     * @brief Parses a 16-bit bitmask into a per-link boolean vector.
     *
     * Bit indices align with ETH channels (logical coordinates).
     *
     * @param bitmask Raw 16-bit status bitmask from telemetry.
     * @return std::vector<bool> Per-link boolean status.
     */
    static std::vector<bool> parse_eth_status_bitmask(uint16_t bitmask);

    /**
     * @brief Reads a raw telemetry value using the feature key's read mechanism.
     * @param key Variant key describing how to read the value (tag type or fixed value).
     * @return uint32_t Raw telemetry value.
     */
    uint32_t read_raw_telemetry(const FeatureKey& key) const;

    /**
     * @brief Checks whether a firmware feature is available in the current configuration.
     * @param feature The firmware feature to check.
     * @return true if the feature is present and readable.
     */
    bool is_feature_available(FirmwareFeature feature) const;

    /**
     * @brief Reads a telemetry feature and applies its configured transform.
     * @tparam T The desired return type after transformation.
     * @param feature The firmware feature to read.
     * @return std::optional<T> The transformed value, or std::nullopt if unavailable.
     */
    template <typename T>
    std::optional<T> read_scalar(FirmwareFeature feature) const;

    /// Non-owning pointer to the telemetry reader, injected at construction.
    FirmwareTelemetryReader* telemetry_reader_ = nullptr;

    /// Firmware bundle version, determined at construction.
    FirmwareBundleVersion firmware_version = FirmwareBundleVersion(0, 0, 0);

    /// Feature-to-telemetry mapping table, selected by architecture and firmware version.
    FirmwareFeatures firmware_feature_map;
};

/** @} */  // end of tt_firmware_info_provider group

}  // namespace tt::umd
