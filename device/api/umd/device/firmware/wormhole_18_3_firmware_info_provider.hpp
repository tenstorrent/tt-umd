/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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
    Wormhole_18_3_FirmwareInfoProvider(TTDevice* tt_device);

    uint64_t get_board_id() const override;

    uint32_t get_eth_fw_version() const override;

    double get_asic_temperature() const override;

    std::vector<DramTrainingStatus> get_dram_training_status(uint32_t num_dram_channels) const override;

    uint32_t get_max_clock_freq() const override;

    uint8_t get_asic_location() const override;

    std::optional<uint32_t> get_aiclk() const override;

    std::optional<uint32_t> get_axiclk() const override;

    std::optional<uint32_t> get_arcclk() const override;

    std::optional<uint32_t> get_fan_speed() const override;

    std::optional<uint32_t> get_tdp() const override;

    std::optional<uint32_t> get_tdc() const override;

    std::optional<uint32_t> get_vcore() const override;

    std::optional<double> get_board_temperature() const override;

    uint32_t get_heartbeat() const override;
};

}  // namespace tt::umd
