/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>

namespace tt::umd {
class semver_t;
class TTDevice;

class FirmwareVersioner {
public:
    static std::unique_ptr<FirmwareVersioner> create_firmware_versioner(TTDevice* tt_device);

    virtual ~FirmwareVersioner() = default;

    virtual semver_t get_firmware_version() = 0;

    virtual semver_t get_minimum_compatible_firmware_version() = 0;

    virtual uint64_t get_board_id() = 0;

protected:
    FirmwareVersioner(TTDevice* tt_device);

    TTDevice* tt_device = nullptr;
};

}  // namespace tt::umd
