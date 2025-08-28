/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>

#include "umd/device/firmware/wormhole_18_4_firmware_info_provider.h"

namespace tt::umd {

/* This class captures Wormhole firmware versions older than 18.4.0.
 * Wormhole devices before this version use SM bus telemetry so all the data
 * is read from that telemetry which is completely different from the newer telemetry
 * used in 18.4.0 and later versions.
 */
class WormholeLegacyFirmwareInfoProvider : public Wormhole_18_4_FirmwareInfoProvider {
public:
    WormholeLegacyFirmwareInfoProvider(TTDevice* tt_device);

    uint64_t get_board_id() override;

    uint32_t get_eth_fw_version() override;

    double get_asic_temperature() override;

    DramTrainingStatus get_dram_training_status(uint32_t dram_channel) override;
};

}  // namespace tt::umd
