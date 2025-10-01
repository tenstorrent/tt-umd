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

    uint64_t get_board_id() override;

    uint32_t get_eth_fw_version() override;

    double get_asic_temperature() override;

    DramTrainingStatus get_dram_training_status(uint32_t dram_channel) override;

    uint32_t get_max_clock_freq() override;

    uint8_t get_asic_location() override;

    std::optional<uint32_t> get_aiclk() override;

    std::optional<uint32_t> get_axiclk() override;

    std::optional<uint32_t> get_arcclk() override;

    std::optional<uint32_t> get_fan_speed() override;

    std::optional<uint32_t> get_tdp() override;

    std::optional<uint32_t> get_tdc() override;

    std::optional<uint32_t> get_vcore() override;

    std::optional<double> get_board_temperature() override;

    uint32_t get_heartbeat() override;
};

}  // namespace tt::umd
