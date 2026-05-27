// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "types.hpp"

namespace tt::umd {

class FirmwareInfoProvider {
public:
    virtual ~FirmwareInfoProvider() = default;
    virtual std::optional<uint64_t> get_board_id() const = 0;
    virtual std::optional<uint8_t> get_asic_location() const = 0;
    virtual std::optional<double> get_asic_temperature() const = 0;
    virtual std::optional<uint32_t> get_aiclk() const = 0;
    virtual uint32_t get_clock_freq() = 0;
    virtual std::optional<uint32_t> get_min_clock_freq() const = 0;
    virtual std::optional<uint32_t> get_max_clock_freq() const = 0;
    virtual FirmwareBundleVersion get_firmware_version() const = 0;
};

}  // namespace tt::umd
