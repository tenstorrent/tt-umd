/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/arc/spi.hpp"

namespace tt::umd {

class TTDevice;

/**
 * Blackhole-specific SPI implementation.
 * Uses dynamic buffer info from SCRATCH_RAM and EEPROM ARC messages.
 */
class BlackholeSPI : public SPI {
public:
    explicit BlackholeSPI(TTDevice* tt_device);

    void read(uint32_t addr, uint8_t* data, size_t size) override;
    void write(uint32_t addr, const uint8_t* data, size_t size) override;

private:
    TTDevice* tt_device_;
};

}  // namespace tt::umd
