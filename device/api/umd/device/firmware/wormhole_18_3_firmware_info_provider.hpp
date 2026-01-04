// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/firmware/wormhole_18_7_firmware_info_provider.hpp"

namespace tt::umd {

/* This class captures Wormhole firmware versions up to version 18.3.0.
 * Wormhole devices before this version use SM bus telemetry so all the data
 * is read from that telemetry which is completely different from the newer telemetry
 * used in 18.4.0 and later versions.
 */
class Wormhole_18_3_FirmwareInfoProvider : public Wormhole_18_7_FirmwareInfoProvider {
public:
    Wormhole_18_3_FirmwareInfoProvider(TTDevice* tt_device, bool use_noc1);

    uint64_t get_board_id(bool use_noc1) const override;

    uint32_t get_eth_fw_version(bool use_noc1) const override;

    std::optional<semver_t> get_eth_fw_version_semver(bool use_noc1) const override;

    std::optional<semver_t> get_gddr_fw_version(bool use_noc1) const override;

    std::optional<semver_t> get_cm_fw_version(bool use_noc1) const override;

    std::optional<semver_t> get_dm_app_fw_version(bool use_noc1) const override;

    std::optional<semver_t> get_dm_bl_fw_version(bool use_noc1) const override;

    std::optional<semver_t> get_tt_flash_version(bool use_noc1) const override;

    double get_asic_temperature(bool use_noc1) const override;

    std::vector<DramTrainingStatus> get_dram_training_status(uint32_t num_dram_channels, bool use_noc1) const override;

    uint32_t get_max_clock_freq(bool use_noc1) const override;

    uint8_t get_asic_location(bool use_noc1) const override;

    std::optional<uint32_t> get_aiclk(bool use_noc1) const override;

    std::optional<uint32_t> get_axiclk(bool use_noc1) const override;

    std::optional<uint32_t> get_arcclk(bool use_noc1) const override;

    std::optional<uint32_t> get_fan_speed(bool use_noc1) const override;

    std::optional<uint32_t> get_tdp(bool use_noc1) const override;

    std::optional<uint32_t> get_tdc(bool use_noc1) const override;

    std::optional<uint32_t> get_vcore(bool use_noc1) const override;

    std::optional<double> get_board_temperature(bool use_noc1) const override;

    uint32_t get_heartbeat(bool use_noc1) const override;
};

}  // namespace tt::umd
