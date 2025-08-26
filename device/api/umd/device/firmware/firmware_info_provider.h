/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>

#include "umd/device/types/cluster_descriptor_types.h"

namespace tt::umd {
class semver_t;
class TTDevice;

class FirmwareInfoProvider {
public:
    static std::unique_ptr<FirmwareInfoProvider> create_firmware_info_provider(TTDevice* tt_device);

    FirmwareInfoProvider(TTDevice* tt_device);

    virtual ~FirmwareInfoProvider() = default;

    virtual semver_t get_firmware_version();

    virtual semver_t get_minimum_compatible_firmware_version();

    virtual uint64_t get_board_id();

    virtual uint32_t get_eth_fw_version();

    virtual double get_asic_temperature();

    virtual DramTrainingStatus get_dram_training_status(uint32_t dram_channel);

protected:
    TTDevice* tt_device = nullptr;

    semver_t firmware_version = semver_t(0, 0, 0);
};

}  // namespace tt::umd
