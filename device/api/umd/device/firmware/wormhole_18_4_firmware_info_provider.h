/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>

#include "umd/device/firmware/wormhole_legacy_firmware_info_provider.h"

namespace tt::umd {

class Wormhole_18_4_FirmwareInfoProvider : public WormholeLegacyFirmwareInfoProvider {
public:
    Wormhole_18_4_FirmwareInfoProvider(TTDevice* tt_device);

    uint64_t get_board_id() override;

    uint32_t get_eth_fw_version() override;

    double get_asic_temperature() override;

    DramTrainingStatus get_dram_training_status(uint32_t dram_channel) override;
};

}  // namespace tt::umd
