// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <optional>

#include "umd/device/firmware/telemetry_mapping.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {
class semver_t;
class TTDevice;

/*
 * FirmwareInfoProvider is a data-driven class that abstracts away the details of specific firmware
 * versions while maintaining backward compatibility. It provides information about the firmware
 * running on the device, such as version, board ID, ethernet firmware version, ASIC temperature,
 * and DRAM training status.
 *
 */
class FirmwareInfoProvider {
public:
    static std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider(TTDevice* tt_device);

    FirmwareInfoProvider(TTDevice* tt_device);

    ~FirmwareInfoProvider() = default;

    semver_t get_firmware_version() const;

    static semver_t get_minimum_compatible_firmware_version(tt::ARCH arch);

    /**
     * This function should capture latest firmware version that is supported by the UMD.
     * It is used to verify that the firmware running on the device is not newer than what UMD supports.
     * The function is meant to change on every FW release, so we can keep track of supported features
     * from new FW versions.
     */
    static semver_t get_latest_supported_firmware_version(tt::ARCH arch);

    uint64_t get_board_id() const;

    uint32_t get_eth_fw_version() const;

    // TODO: remove semver suffix from this function when client code is changed to use semver_t directly.
    // Remove version of the function that returns uint32_t accordingly.
    std::optional<semver_t> get_eth_fw_version_semver() const;

    std::optional<semver_t> get_gddr_fw_version() const;

    std::optional<semver_t> get_cm_fw_version() const;

    std::optional<semver_t> get_dm_app_fw_version() const;

    std::optional<semver_t> get_dm_bl_fw_version() const;

    std::optional<semver_t> get_tt_flash_version() const;

    /*
     * Get ASIC temperature in Celsius.
     * @returns ASIC temperature [Celsius]
     */
    double get_asic_temperature() const;

    /*
     * Get AICLK in MHz.
     * @returns AICLK [MHz]
     */
    std::optional<uint32_t> get_aiclk() const;

    /*
     * Get AXICLK in MHz.
     * @returns AXICLK [MHz]
     */
    std::optional<uint32_t> get_axiclk() const;

    /*
     * Get ARCCLK in MHz.
     * @returns ARCCLK [MHz]
     */
    std::optional<uint32_t> get_arcclk() const;

    /*
     * Get fan speed in rpm, if fans are present and controllable by firmware.
     * @returns Fan speed [rpm]
     */
    std::optional<uint32_t> get_fan_speed() const;

    /*
     * Get TDP in watts.
     * @returns TDP [W]
     */
    std::optional<uint32_t> get_tdp() const;

    /*
     * Get TDC in amps.
     * @returns TDC [amps]
     */
    std::optional<uint32_t> get_tdc() const;

    /*
     * Get VCORE in mV.
     * @returns VCORE [mV]
     */
    std::optional<uint32_t> get_vcore() const;

    /*
     * Get board temperature in Celsius.
     * @returns Board temperature [Celsius]
     */
    std::optional<double> get_board_temperature() const;

    std::vector<DramTrainingStatus> get_dram_training_status(uint32_t num_dram_channels) const;

    uint32_t get_max_clock_freq() const;

    uint8_t get_asic_location() const;

    /*
     * Get heartbeat from ARC core.
     * If using current telemetry, the value is taken from TIMER_HEARTBEAT
     * On legacy telemetry, the value is taken from ARC0_HEALTH
     * @returns An integer that does not decrease on subsequent calls.
     */
    uint32_t get_heartbeat() const;

private:
    TTDevice* tt_device = nullptr;

    semver_t firmware_version = semver_t(0, 0, 0);

    // Configuration map that drives the data-driven behavior.
    TelemetryFeatureMap telemetry_feature_map;

    // Factory helpers for creating telemetry feature configuration maps.
    static TelemetryFeatureMap create_telemetry_feature_map(TTDevice* tt_device, const semver_t& fw_version);
    static TelemetryFeatureMap create_modern_base();
    static TelemetryFeatureMap create_legacy_wormhole_18_3_base();

    // Engine methods for reading and transforming telemetry data.
    uint32_t read_raw_telemetry(const TelemetryKey& key) const;

    bool is_feature_available(FirmwareFeature feature) const;

    template <typename T>
    std::optional<T> read_scalar(FirmwareFeature feature) const;
};

}  // namespace tt::umd
