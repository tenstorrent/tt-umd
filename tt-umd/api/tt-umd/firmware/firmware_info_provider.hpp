// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/gddr_telemetry.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/telemetry.hpp"
#include "tt-umd/utils/semver.hpp"

namespace tt::umd {

class FirmwareInfoProvider {
public:
    virtual ~FirmwareInfoProvider() = default;

    /**
     * @brief Retrieves the firmware bundle version running on the device.
     * @param noc_id NOC to route through.
     * @return FirmwareBundleVersion The active firmware version.
     */
    virtual FirmwareBundleVersion get_firmware_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the Ethernet firmware version as a raw tag value.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Raw version tag, or std::nullopt if unavailable.
     */
    [[deprecated("Use get_eth_fw_version_semver()")]] virtual std::optional<uint32_t> get_eth_fw_version(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the Ethernet firmware version as a semantic version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_eth_fw_version_semver(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the GDDR firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_gddr_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the CM (Clock Manager) firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_cm_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the DM application firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_dm_app_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the DM bootloader firmware version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_dm_bl_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the TT-Flash version.
     * @param noc_id NOC to route through.
     * @return std::optional<SemVer> Parsed version, or std::nullopt if unavailable.
     */
    virtual std::optional<SemVer> get_tt_flash_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the unique board identifier.
     * @param noc_id NOC to route through.
     * @return std::optional<uint64_t> Board ID, or std::nullopt if unavailable.
     */
    virtual std::optional<uint64_t> get_board_id([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the physical slot index of this chip on a multi-chip board.
     * @param noc_id NOC to route through.
     * @return std::optional<uint8_t> ASIC location index, or std::nullopt if unavailable.
     */
    virtual std::optional<uint8_t> get_asic_location([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the ASIC temperature.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_asic_temperature([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the board temperature.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_board_temperature([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the thermal shutdown threshold.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Threshold in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_thm_limit_shutdown([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the thermal throttle threshold.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Threshold in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_thm_limit_throttle([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the thermal trip count.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Number of thermal trips, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_therm_trip_count([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the current AICLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_aiclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the current AICLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the current AXICLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_axiclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the current ARCCLK frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_arcclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the maximum supported clock frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_max_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the minimum supported clock frequency.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_min_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the Thermal Design Power.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> TDP in watts, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_tdp([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the Thermal Design Power limit.
     * @param noc_id NOC to route through.
     * @returns std::optional<uint32_t> TDP limit in watts, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_tdp_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the Thermal Design Current.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> TDC in amps, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_tdc([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the core voltage.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> VCORE in mV, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_vcore([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the board power limit.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Power limit in watts, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_board_power_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the fan speed as a percentage (0-100).
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Fan speed in percent, or std::nullopt if unavailable.
     */
    [[deprecated("use get_fan_speeds()")]] virtual std::optional<uint32_t> get_fan_speed(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the fan speed in RPM.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Fan speed in RPM, or std::nullopt if unavailable.
     */
    [[deprecated("use get_fan_rpms()")]] virtual std::optional<uint32_t> get_fan_rpm(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves targeted speeds per fan as a percentage (0-100).
     * Individual entries can be std::nullopt if the percentage is unavailable.
     * @param noc_id NOC to route through.
     * @return std::vector<std::optional<uint32_t>> of targeted fan speeds as a percentage.
     */
    virtual std::vector<std::optional<uint32_t>> get_fan_speeds(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves actual speeds per fan in rotations per minute.
     * Individual entries can be std::nullopt if fan RPM is unavailable.
     * @param noc_id NOC to route through.
     * @return std::vector<std::optional<uint32_t>> of actual fan speeds in RPM.
     */
    virtual std::vector<std::optional<uint32_t>> get_fan_rpms(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves per-link Ethernet heartbeat status.
     *
     * Vector indices align with ETH channels (logical coordinates, up to 16).
     *
     * @param noc_id NOC to route through.
     * @return std::optional<std::vector<bool>> Per-link status (true = active), or std::nullopt if unavailable.
     */
    [[deprecated("Use get_eth_heartbeat_status_per_core()")]] virtual std::optional<std::vector<bool>>
    get_eth_heartbeat_status([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves per-link Ethernet retrain status.
     *
     * Vector indices align with ETH channels (logical coordinates, up to 16).
     *
     * @param noc_id NOC to route through.
     * @return std::optional<std::vector<bool>> Per-link status (true = retrained), or std::nullopt if unavailable.
     */
    [[deprecated("Use get_eth_retrain_status_per_core()")]] virtual std::optional<std::vector<bool>>
    get_eth_retrain_status([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Get per-link ethernet heartbeat status.
     * Available on Wormhole (all versions) and Blackhole (firmware 19.9+); returns std::nullopt otherwise.
     * Each entry pairs an ETH core's NOC0 coordinate with its status (16 entries on WH, 14 on BH).
     * @param noc_id NOC to route through.
     * @returns Per-core heartbeat status (true = active), or std::nullopt if unavailable.
     */
    virtual std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_heartbeat_status_per_core(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Get per-link ethernet retrain status.
     * Only available on Wormhole with firmware prior to 19.9; returns std::nullopt otherwise.
     * Each entry pairs an ETH core's NOC0 coordinate with its status.
     * @param noc_id NOC to route through.
     * @returns Per-core retrain status (true = retrained), or std::nullopt if unavailable.
     */
    virtual std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_retrain_status_per_core(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Get per-link ethernet link status.
     * Available on firmware 19.9+ for both Wormhole and Blackhole; returns std::nullopt otherwise.
     * Each entry pairs an ETH core's NOC0 coordinate with its status.
     * @param noc_id NOC to route through.
     * @returns Per-core link status (true = up), or std::nullopt if unavailable.
     */
    virtual std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_link_status_per_core(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves per-channel DRAM training status.
     * @param num_dram_channels Number of DRAM channels to query.
     * @param noc_id NOC to route through.
     * @return std::vector<DramTrainingStatus> Per-channel training status. Empty if unavailable.
     */
    virtual std::vector<DramTrainingStatus> get_dram_training_status(
        uint32_t num_dram_channels, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves aggregated telemetry across all GDDR modules.
     * @param noc_id NOC to route through.
     * @return std::optional<GddrTelemetry> Aggregated DRAM telemetry, or std::nullopt if unavailable.
     */
    virtual std::optional<GddrTelemetry> get_aggregated_dram_telemetry(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves telemetry for a specific GDDR module.
     * @param gddr_module The target GDDR module.
     * @param noc_id NOC to route through.
     * @return std::optional<GddrModuleTelemetry> Module telemetry, or std::nullopt if unavailable.
     */
    virtual std::optional<GddrModuleTelemetry> get_dram_telemetry(
        GddrModule gddr_module, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the GDDR speed.
     * @param noc_id NOC to route through.
     * @return std::optional<uint16_t> Speed value, or std::nullopt if unavailable.
     */
    virtual std::optional<uint16_t> get_dram_speed([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the current maximum DRAM temperature across all modules.
     * @param noc_id NOC to route through.
     * @return std::optional<double> Temperature in degrees Celsius, or std::nullopt if unavailable.
     */
    virtual std::optional<double> get_current_max_dram_temperature(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the firmware heartbeat counter.
     *
     * A monotonically non-decreasing value that indicates the firmware is alive.
     *
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Heartbeat counter, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_heartbeat([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the highest core voltage (VDD) the firmware is allowed to request.
     * A static chip limit from the board's firmware table.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Voltage in mV, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_vdd_max([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the lowest core voltage (VDD) the firmware is allowed to request.
     * A static chip limit from the board's firmware table.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Voltage in mV, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_vdd_min([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the current clock of each L2CPU (RISC-V) cluster.
     * Blackhole only. The vector always has one entry per cluster (4), and an entry is std::nullopt when that
     * cluster's clock is unavailable. Harvested clusters still report a clock; use get_enabled_l2cpus() to tell
     * which clusters are usable.
     * @param noc_id NOC to route through.
     * @return std::vector<std::optional<uint32_t>> Frequency in MHz per cluster, indexed by cluster.
     */
    virtual std::vector<std::optional<uint32_t>> get_l2cpu_clocks(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves which Tensix columns are enabled (not harvested).
     * Bit i is set when physical Tensix column i is usable. Blackhole has 14 columns. The bit order is the
     * firmware's physical order, not the logical order used by SocDescriptor harvesting masks.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Bitmask of enabled columns, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_enabled_tensix_columns(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves which Tensix rows are enabled (not harvested).
     * Bit i is set when physical Tensix row i is usable. Only meaningful on architectures that harvest Tensix by
     * row. Blackhole firmware does not publish it.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Bitmask of enabled rows, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_enabled_tensix_rows(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves which Ethernet cores are enabled (not harvested).
     * Bit i is set when physical Ethernet channel i may be used. An enabled channel is not necessarily
     * cabled to anything. Blackhole has 14 channels.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Bitmask of enabled channels, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_enabled_eth_cores([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves which GDDR instances are enabled (not harvested).
     * Bit i is set when GDDR instance i is usable. Blackhole has 8 instances.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Bitmask of enabled instances, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_enabled_gddr([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves which L2CPU clusters are enabled (not harvested).
     * Blackhole only. Bit i is set when L2CPU cluster i is usable. Blackhole has 4 clusters.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Bitmask of enabled clusters, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_enabled_l2cpus([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the configured mode of each PCIe instance.
     * The vector always has one entry per PCIe instance (2). An entry is std::nullopt when the mode is unavailable
     * or the firmware reports a mode this API does not know.
     * @param noc_id NOC to route through.
     * @return std::vector<std::optional<PcieMode>> Mode per PCIe instance, indexed by instance.
     */
    virtual std::vector<std::optional<PcieMode>> get_pcie_modes(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the board's total input power, as measured at the board's power input.
     * Includes everything on the board, not just the ASIC (compare get_tdp()).
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Power in watts, truncated to an integer, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_input_power([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the Thermal Design Current limit the firmware throttles AICLK to stay under.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> TDC limit in amps, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_tdc_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the power drawn by the GDDR IO rail on the west side of the chip.
     * Blackhole only. Derived by firmware from the rail current and its fixed 1.35 V supply.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Power in watts, truncated to an integer, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_gddr_west_io_power(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the power drawn by the GDDR IO rail on the east side of the chip.
     * Blackhole only. Derived by firmware from the rail current and its fixed 1.35 V supply.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Power in watts, truncated to an integer, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_gddr_east_io_power(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the AICLK arbitration state: the effective floor and ceiling, which arbiters are enabled and
     * which ones set the floor, the ceiling and the current target. Blackhole only.
     * Each firmware telemetry word is read once and every field packed into it is decoded from that read, so a
     * frequency is never paired with an arbiter from a different telemetry update. The words themselves are still read
     * one after another, so the fields taken from different words are not an atomic snapshot.
     * Availability follows the firmware release: the frequencies and enabled arbiters are published from 19.5, while
     * the winning arbiters and target reason (AiclkArbitration::min_arbiter, max_arbiter, target_reason and
     * target_arbiter) are only set from 19.7.
     * @param noc_id NOC to route through.
     * @return std::optional<AiclkArbitration> The arbitration state, or std::nullopt if the firmware does not publish
     * it.
     */
    virtual std::optional<AiclkArbitration> get_aiclk_arbitration(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the AICLK ceiling set by the host.
     * Blackhole only. 0 means the host has not set a limit; any other value is enforced by the HOST_FMAX arbiter.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_host_aiclk_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the number of errors the ARC's SMBus target has seen in its traffic with the board
     * management controller. Blackhole only. Counts since the chip booted.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Error count, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_smbus_error_count([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Reports whether the firmware supports an optional capability. Blackhole only.
     * Firmware publishes capability flags from 19.13. The kernel NOP throttler is also reported as supported on
     * 19.12, which has the throttler but no flags yet.
     * @param capability The capability to query.
     * @param noc_id NOC to route through.
     * @return std::optional<bool> true if supported, or std::nullopt if the firmware does not say.
     */
    virtual std::optional<bool> is_capability_supported(
        FirmwareCapability capability, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Reports whether a capability is currently switched on. Blackhole only.
     * @param capability The capability to query.
     * @param noc_id NOC to route through.
     * @return std::optional<bool> true if enabled, or std::nullopt if the capability is not supported or the firmware
     * does not say.
     */
    virtual std::optional<bool> is_capability_enabled(
        FirmwareCapability capability, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves the AICLK at which the kernel NOP throttler stops inserting NOPs.
     * Blackhole only. 0 means the firmware uses the effective AICLK floor (see AiclkArbitration::min_freq_mhz) as the
     * stop frequency. std::nullopt when the firmware does not support FirmwareCapability::KERNEL_NOP_THROTTLER.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Frequency in MHz, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_kernel_nop_throttler_stop_freq(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves how many times the firmware has started making the Tensix cores execute NOPs since the chip
     * booted. Blackhole only. Monotonically increasing. Counts every NOP source: the kernel NOP throttler and
     * critical di/dt (Doppler) throttling, so it can be non-zero even when the kernel NOP throttler is disabled.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Start count, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_nop_start_count([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Retrieves how long the Tensix cores were made to execute NOPs during the firmware's most recent
     * telemetry update interval. Blackhole only. This is a per-interval value, not a running total, so it is at
     * most the telemetry update interval. Counts every NOP source, like get_nop_start_count().
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Duration in milliseconds, or std::nullopt if unavailable.
     */
    virtual std::optional<uint32_t> get_nop_on_duration([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;
};
}  // namespace tt::umd
