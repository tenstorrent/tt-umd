// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "tt_enums_structs_constants_doxy.hpp"

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
 * All telemetry getters return `std::nullopt` when the value is not
 * available for the current firmware version.
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

class FirmwareInfoProvider {
public:
    virtual ~FirmwareInfoProvider() = default;

    /** @name Firmware Versions */
    /** @{ */

    /**
     * @brief Retrieves the firmware bundle version running on the device.
     * @return FirmwareBundleVersion The active firmware version.
     */
    virtual FirmwareBundleVersion get_firmware_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the Ethernet firmware version as a raw tag value.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Raw version tag, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_eth_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the Ethernet firmware version as a semantic version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_eth_fw_version_semver([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the GDDR firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_gddr_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the CM (Clock Manager) firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_cm_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the DM application firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_dm_app_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the DM bootloader firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_dm_bl_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the TT-Flash version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_tt_flash_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */

    /** @name Board Identity */
    /** @{ */

    /**
     * @brief Retrieves the unique board identifier.
     * @return std::optional<uint64_t> Board ID, or std::nullopt if unavailable.
     */
    virtual std::optional<uint64_t> get_board_id([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the physical slot index of this chip on a multi-chip board.
     * @param noc_id NOC to route through.
     * @return std::optional<uint8_t> ASIC location index, or std::nullopt if unavailable.
     */
    virtual std::optional<uint8_t> get_asic_location([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */

    /** @name Thermal */
    /** @{ */

    /**
     * @brief Retrieves the ASIC temperature.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_asic_temperature([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the board temperature.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_board_temperature([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the thermal shutdown threshold.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Threshold in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_thm_limit_shutdown([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the thermal throttle threshold.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Threshold in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_thm_limit_throttle([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the thermal trip count.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Number of thermal trips, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_therm_trip_count([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */

    /** @name Clocks and Power */
    /** @{ */

    /**
     * @brief Retrieves the current AICLK frequency.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_aiclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the current AICLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the current AXICLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_axiclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the current ARCCLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_arcclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the maximum supported clock frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_max_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the minimum supported clock frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_min_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the Thermal Design Power.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> TDP in watts, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_tdp([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the Thermal Design Current.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> TDC in amps, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_tdc([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the core voltage.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> VCORE in mV, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_vcore([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the board power limit.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Power limit in watts, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_board_power_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the fan speed as a percentage (0-100).
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Fan speed in percent, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_fan_speed([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the fan speed in RPM.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Fan speed in RPM, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_fan_rpm([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */

    /** @name Ethernet */
    /** @{ */

    /**
     * @brief Retrieves per-link Ethernet heartbeat status.
     *
     * Vector indices align with ETH channels (logical coordinates, up to 16).
     *
     * @return std::optional<std::vector<bool>> Per-link status (true = active), or std::nullopt if unavailable.
     */
    virtual std::optional<std::vector<bool>> get_eth_heartbeat_status(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves per-link Ethernet retrain status.
     *
     * Vector indices align with ETH channels (logical coordinates, up to 16).
     *
     * @param noc_id NOC to route through.
     * @return std::optional<std::vector<bool>> Per-link status (true = retrained), or std::nullopt if unavailable.
     */
    virtual std::optional<std::vector<bool>> get_eth_retrain_status(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */

    /** @name DRAM */
    /** @{ */

    /**
     * @brief Retrieves per-channel DRAM training status.
     * @param num_dram_channels Number of DRAM channels to query.
     * @return std::vector<DramTrainingStatus> Per-channel training status. Empty if unavailable.
     */
    virtual std::vector<DramTrainingStatus> get_dram_training_status(
        uint32_t num_dram_channels, [[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves aggregated telemetry across all GDDR modules.
     * @param noc_id NOC to route through.
     * @return std::optional<GddrTelemetry> Aggregated DRAM telemetry, or std::nullopt if unavailable.
     */
    virtual std::optional<GddrTelemetry> get_aggregated_dram_telemetry(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves telemetry for a specific GDDR module.
     * @param gddr_module The target GDDR module.
     * @param noc_id NOC to route through.
     * @return std::optional<GddrModuleTelemetry> Module telemetry, or std::nullopt if unavailable.
     */
    virtual std::optional<GddrModuleTelemetry> get_dram_telemetry(
        GddrModule gddr_module, [[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the GDDR speed.
     * @param noc_id NOC to route through.
     * @return std::optional<uint16_t> Speed value, or std::nullopt if unavailable.
     */
    virtual std::optional<uint16_t> get_dram_speed([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /**
     * @brief Retrieves the current maximum DRAM temperature across all modules.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_current_max_dram_temperature(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */

    /** @name Heartbeat */
    /** @{ */

    /**
     * @brief Retrieves the firmware heartbeat counter.
     *
     * A monotonically non-decreasing value that indicates the firmware is alive.
     *
     * @return std::optional<uint32_t> Heartbeat counter, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_heartbeat([[maybe_unused]] NocId noc_id = NocId::DEFAULT) const = 0;

    /** @} */
};

/** @} */  // end of tt_firmware_info_provider group

/**
 * @brief Resolves a board type from its unique board identifier.
 * @param board_id The unique physical board identifier.
 * @return BoardType The board model, or BoardType::UNKNOWN if unrecognized.
 */
inline BoardType get_board_type_from_board_id(uint64_t board_id) { return BoardType::UNKNOWN; }

}  // namespace tt::umd
