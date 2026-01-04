// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <optional>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"

namespace tt::umd {
class semver_t;
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
    static std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider(TTDevice* tt_device, bool use_noc1);

    FirmwareInfoProvider(TTDevice* tt_device, bool use_noc1);

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

    virtual uint64_t get_board_id(bool use_noc1 = false) const;

    virtual uint32_t get_eth_fw_version(bool use_noc1 = false) const;

    // TODO: remove semver suffix from this function when client code is changed to use semver_t directly.
    // Remove version of the function that returns uint32_t accordingly.
    virtual std::optional<semver_t> get_eth_fw_version_semver(bool use_noc1 = false) const;

    virtual std::optional<semver_t> get_gddr_fw_version(bool use_noc1 = false) const;

    virtual std::optional<semver_t> get_cm_fw_version(bool use_noc1 = false) const;

    virtual std::optional<semver_t> get_dm_app_fw_version(bool use_noc1 = false) const;

    virtual std::optional<semver_t> get_dm_bl_fw_version(bool use_noc1 = false) const;

    virtual std::optional<semver_t> get_tt_flash_version(bool use_noc1 = false) const;

    /*
     * Get ASIC temperature in Celsius.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns ASIC temperature [Celsius]
     */
    virtual double get_asic_temperature(bool use_noc1 = false) const;

    /*
     * Get AICLK in MHz.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns AICLK [MHz]
     */
    virtual std::optional<uint32_t> get_aiclk(bool use_noc1 = false) const;

    /*
     * Get AXICLK in MHz.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns AXICLK [MHz]
     */
    virtual std::optional<uint32_t> get_axiclk(bool use_noc1 = false) const;

    /*
     * Get ARCCLK in MHz.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns ARCCLK [MHz]
     */
    virtual std::optional<uint32_t> get_arcclk(bool use_noc1 = false) const;

    /*
     * Get fan speed in rpm, if fans are present and controllable by firmware.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns Fan speed [rpm]
     */
    virtual std::optional<uint32_t> get_fan_speed(bool use_noc1 = false) const;

    /*
     * Get TDP in watts.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns TDP [W]
     */
    virtual std::optional<uint32_t> get_tdp(bool use_noc1 = false) const;

    /*
     * Get TDC in amps.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns TDC [amps]
     */
    virtual std::optional<uint32_t> get_tdc(bool use_noc1 = false) const;

    /*
     * Get VCORE in mV.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns VCORE [mV]
     */
    virtual std::optional<uint32_t> get_vcore(bool use_noc1 = false) const;

    /*
     * Get board temperature in Celsius.
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns Board temperature [Celsius]
     */
    virtual std::optional<double> get_board_temperature(bool use_noc1 = false) const;

    virtual std::vector<DramTrainingStatus> get_dram_training_status(
        uint32_t num_dram_channels, bool use_noc1 = false) const;

    virtual uint32_t get_max_clock_freq(bool use_noc1 = false) const;

    virtual uint8_t get_asic_location(bool use_noc1 = false) const;

    /*
     * Get heartbeat from ARC core.
     * If using current telemetry, the value is taken from TIMER_HEARTBEAT
     * On legacy telemetry, the value is taken from ARC0_HEALTH
     * @param use_noc1 Whether to read telemetry from NOC1 or NOC0
     * @returns An integer that does not decrease on subsequent calls.
     */
    virtual uint32_t get_heartbeat(bool use_noc1 = false) const;

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
