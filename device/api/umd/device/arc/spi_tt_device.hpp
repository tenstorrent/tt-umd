/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tt::umd {

class TTDevice;

/**
 * Interface to the SPI flash memory on Tenstorrent devices.
 * This SPI flash stores device images including ARC firmware, ETH base firmware, and other system images.
 * This class takes a pointer to TTDevice on construction and provides
 * read/write operations to the SPI flash memory.
 */
class SPITTDevice {
public:
    /**
     * Constructor that takes a pointer to TTDevice.
     *
     * @param device Pointer to the TTDevice to use for SPI operations
     */
    explicit SPITTDevice(TTDevice *device);

    virtual ~SPITTDevice() = default;

    /**
     * Read data from SPI flash memory.
     *
     * @param addr SPI address to read from
     * @param data Buffer to store the read data
     * @param size Number of bytes to read
     */
    void read(uint32_t addr, uint8_t *data, size_t size);

    /**
     * Write data to SPI flash memory.
     *
     * @param addr SPI address to write to
     * @param data Buffer containing data to write
     * @param size Number of bytes to write
     * @param skip_write_to_spi If true, the data will not be committed to SPI. This is useful for testing.
     */
    void write(uint32_t addr, const uint8_t *data, size_t size, bool skip_write_to_spi = false);

protected:
    TTDevice *device_;
};

}  // namespace tt::umd
