/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/tt_device/firmware_versioner.h"

namespace tt::umd {

class TTDevice;

class BlackholeFirmwareVersioner : public FirmwareVersioner {
public:
    BlackholeFirmwareVersioner(TTDevice* tt_device);

    semver_t get_firmware_version() override;

    semver_t get_minimum_compatible_firmware_version() override;

    uint64_t get_board_id() override;
};

}  // namespace tt::umd
