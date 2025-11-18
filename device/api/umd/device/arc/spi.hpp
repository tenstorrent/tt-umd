/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::umd {

class TTDevice;

/**
 * Abstract interface for SPI operations on Tenstorrent devices.
 * Different architectures implement this interface differently.
 */
class SPI {
public:
    virtual ~SPI() = default;

    /**
     * Read data from SPI flash memory.
     *
     * @param addr SPI address to read from
     * @param data Buffer to store the read data
     * @param size Number of bytes to read
     */
    virtual void read(uint32_t addr, uint8_t* data, size_t size) = 0;

    /**
     * Write data to SPI flash memory.
     *
     * @param addr SPI address to write to
     * @param data Buffer containing data to write
     * @param size Number of bytes to write
     * @param skip_write_to_spi If true, the data will not be committed to SPI. This is useful for testing.
     */
    virtual void write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi = false) = 0;
};

}  // namespace tt::umd
