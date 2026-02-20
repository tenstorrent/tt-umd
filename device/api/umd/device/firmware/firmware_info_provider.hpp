// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <optional>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {
class TTDevice;

/*
 * FirmwareInfoProvider is a class that should abstract away the details of specific firmware version
 * as well as keep backward compatibility with older firmware versions. It should provide
 * information about the firmware running on the device, such as version, board ID, ethernet
 * firmware version, ASIC temperature, and DRAM training status.
 * The idea behind the design is that base class provides most up to date functionality, while
 * derived classes can override methods to provide backward compatibility with older firmware versions.
 * For examples, look at Wormhole_18_3_FirmwareInfoProvider and WormholeLegacyFirmwareInfoProvider classes.
 */
class FirmwareInfoProvider {
public:
    static std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider(TTDevice* tt_device);

    FirmwareInfoProvider(TTDevice* tt_device);

    virtual ~FirmwareInfoProvider() = default;

    virtual semver_t get_firmware_version() const;

    static semver_t get_minimum_compatible_firmware_version(tt::ARCH arch);

    /**
     * This function should capture latest firmware version that is supported by the UMD.
     * It is used to verify that the firmware running on the device is not newer than what UMD supports.
     * The function is meant to change on every FW release, so we can keep track of supported features
     * from new FW versions.
     */
    static semver_t get_latest_supported_firmware_version(tt::ARCH arch);

    virtual uint64_t get_board_id() const;

    virtual uint32_t get_eth_fw_version() const;

    // TODO: remove semver suffix from this function when client code is changed to use semver_t directly.
    // Remove version of the function that returns uint32_t accordingly.
    virtual std::optional<semver_t> get_eth_fw_version_semver() const;

    virtual std::optional<semver_t> get_gddr_fw_version() const;

    virtual std::optional<semver_t> get_cm_fw_version() const;

    virtual std::optional<semver_t> get_dm_app_fw_version() const;

    virtual std::optional<semver_t> get_dm_bl_fw_version() const;

    virtual std::optional<semver_t> get_tt_flash_version() const;

    /*
     * Get ASIC temperature in Celsius.
     * @returns ASIC temperature [Celsius]
     */
    virtual double get_asic_temperature() const;

    /*
     * Get AICLK in MHz.
     * @returns AICLK [MHz]
     */
    virtual std::optional<uint32_t> get_aiclk() const;

    /*
     * Get AXICLK in MHz.
     * @returns AXICLK [MHz]
     */
    virtual std::optional<uint32_t> get_axiclk() const;

    /*
     * Get ARCCLK in MHz.
     * @returns ARCCLK [MHz]
     */
    virtual std::optional<uint32_t> get_arcclk() const;

    /*
     * Get fan speed in rpm, if fans are present and controllable by firmware.
     * @returns Fan speed [rpm]
     */
    virtual std::optional<uint32_t> get_fan_speed() const;

    /*
     * Get TDP in watts.
     * @returns TDP [W]
     */
    virtual std::optional<uint32_t> get_tdp() const;

    /*
     * Get TDC in amps.
     * @returns TDC [amps]
     */
    virtual std::optional<uint32_t> get_tdc() const;

    /*
     * Get VCORE in mV.
     * @returns VCORE [mV]
     */
    virtual std::optional<uint32_t> get_vcore() const;

    /*
     * Get board temperature in Celsius.
     * @returns Board temperature [Celsius]
     */
    virtual std::optional<double> get_board_temperature() const;

    virtual std::vector<DramTrainingStatus> get_dram_training_status(uint32_t num_dram_channels) const;

    virtual uint32_t get_max_clock_freq() const;

    virtual uint8_t get_asic_location() const;

    /*
     * Get heartbeat from ARC core.
     * If using current telemetry, the value is taken from TIMER_HEARTBEAT
     * On legacy telemetry, the value is taken from ARC0_HEALTH
     * @returns An integer that does not decrease on subsequent calls.
     */
    virtual uint32_t get_heartbeat() const;

protected:
    TTDevice* tt_device = nullptr;

    semver_t firmware_version = semver_t(0, 0, 0);

    bool aiclk_available;
    bool axiclk_available;
    bool arcclk_available;
    bool fan_speed_available;
    bool tdp_available;
    bool tdc_available;
    bool vcore_available;
    bool board_temperature_available;
};

}  // namespace tt::umd
