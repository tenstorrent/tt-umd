// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "umd/device/arc/firmware_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/firmware/firmware_telemetry_mapping.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/gddr_telemetry.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

namespace tt::umd {
class TTDevice;

/*
 * FirmwareInfoProvider is a data-driven class that abstracts away the details of specific firmware
 * versions while maintaining backward compatibility. It provides information about the firmware
 * running on the device, such as version, board ID, ethernet firmware version, ASIC temperature,
 * and DRAM training status.
 *
 */
class FirmwareInfoProviderImplementation : public FirmwareInfoProvider {
public:
    static std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider(TTDevice* tt_device);

    FirmwareInfoProviderImplementation(TTDevice* tt_device, FirmwareBundleVersion firmware_version);

    virtual FirmwareBundleVersion get_firmware_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint64_t> get_board_id([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    // TODO: Will be removed in UMD Base API 2.0.0 (#3181)
    [[deprecated("Use get_eth_fw_version_semver()")]] virtual std::optional<uint32_t> get_eth_fw_version(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<SemVer> get_eth_fw_version_semver([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<SemVer> get_gddr_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<SemVer> get_cm_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<SemVer> get_dm_app_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<SemVer> get_dm_bl_fw_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<SemVer> get_tt_flash_version([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<double> get_asic_temperature([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_aiclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_axiclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_arcclk([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    [[deprecated("use get_fan_speeds()")]] virtual std::optional<uint32_t> get_fan_speed(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    [[deprecated("use get_fan_rpms()")]] virtual std::optional<uint32_t> get_fan_rpm(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::vector<std::optional<uint32_t>> get_fan_speeds(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::vector<std::optional<uint32_t>> get_fan_rpms([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_tdp([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_tdc([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_vcore([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<double> get_board_temperature([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<double> get_thm_limit_shutdown([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_tdp_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_board_power_limit([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<double> get_thm_limit_throttle([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_therm_trip_count([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    [[deprecated("Use get_eth_heartbeat_status_per_core()")]] virtual std::optional<std::vector<bool>>
    get_eth_heartbeat_status([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    [[deprecated("Use get_eth_retrain_status_per_core()")]] virtual std::optional<std::vector<bool>>
    get_eth_retrain_status([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_heartbeat_status_per_core(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_retrain_status_per_core(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<std::vector<std::pair<CoreCoord, bool>>> get_eth_link_status_per_core(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::vector<DramTrainingStatus> get_dram_training_status(
        uint32_t num_dram_channels, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_max_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_min_clock_freq([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint8_t> get_asic_location([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_heartbeat([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<GddrTelemetry> get_aggregated_dram_telemetry(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<GddrModuleTelemetry> get_dram_telemetry(
        GddrModule gddr_module, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint16_t> get_dram_speed([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<double> get_current_max_dram_temperature(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_runtime_telemetry_buffer_address(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

    virtual std::optional<uint32_t> get_runtime_telemetry_buffer_size(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const;

private:
    /**
     * Parse a 16-bit bitmask into per-core status using the arch-specific bit-to-NOC0 mapping.
     */
    std::vector<std::pair<CoreCoord, bool>> parse_eth_status_bitmask(uint16_t bitmask) const;

    tt::ARCH arch_ = ARCH::Invalid;
    DeviceProtocol* device_protocol_ = nullptr;
    FirmwareTelemetryReader* telemetry_ = nullptr;
    std::unique_ptr<SmBusArcTelemetryReader> smbus_telemetry_ = nullptr;
    xy_pair arc_core_noc0_;

    FirmwareBundleVersion firmware_version = FirmwareBundleVersion(0, 0, 0);

    // Configuration map that drives the data-driven behavior.
    FirmwareFeatures firmware_feature_map;

    // Factory helpers for creating telemetry feature configuration maps.
    static FirmwareFeatures create_firmware_feature_map(tt::ARCH arch, const FirmwareBundleVersion& fw_version);
    static FirmwareFeatures create_18_4_new_telemetry_base();
    static FirmwareFeatures create_wormhole_18_3_base();
    static FirmwareFeatures create_wormhole_18_4_base();
    static FirmwareFeatures create_blackhole_18_5_base();
    static FirmwareFeatures create_wormhole_18_8_base();
    static FirmwareFeatures create_wormhole_19_9_base();
    static FirmwareFeatures create_blackhole_18_8_base();
    static FirmwareFeatures create_blackhole_19_8_base();
    static FirmwareFeatures create_blackhole_19_9_base();

    // Engine methods for reading and transforming telemetry data.
    uint32_t read_raw_telemetry(const FeatureKey& key) const;

    bool is_feature_available(FirmwareFeature feature) const;

    template <typename T>
    std::optional<T> read_scalar(FirmwareFeature feature) const;

    /**
     * @brief Maximum number of fans supported by the device.
     * TODO: SysEng should provide a proper way of querying the number of fans on the device.
     */
    static constexpr size_t MAX_NUMBER_OF_FANS = 2U;
};

}  // namespace tt::umd
