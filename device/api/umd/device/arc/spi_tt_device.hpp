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
     * Factory method that creates the appropriate SPI implementation based on device architecture.
     *
     * @param device Pointer to the TTDevice to use for SPI operations
     * @return Unique pointer to the appropriate SPITTDevice implementation
     */
    static std::unique_ptr<SPITTDevice> create(TTDevice *device);

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
    virtual void read(uint32_t addr, uint8_t *data, size_t size) = 0;

    /**
     * Write data to SPI flash memory.
     *
     * @param addr SPI address to write to
     * @param data Buffer containing data to write
     * @param size Number of bytes to write
     * @param skip_write_to_spi If true, the data will not be committed to SPI. This is useful for testing.
     */
    virtual void write(uint32_t addr, const uint8_t *data, size_t size, bool skip_write_to_spi = false) = 0;
        
    /**
     * Get the firmware bundle version by reading from SPI flash (Blackhole only).
     *
     * The raw 32-bit value format: [component][major][minor][patch] (each 8 bits)
     * Returns as semver_t with major.minor.patch (component byte is not included)
     *
     * @return The firmware bundle version as semver_t
     * @throws std::runtime_error if not supported on this architecture or cannot read from SPI
     */
     virtual uint32_t get_spi_fw_bundle_version();

protected:
    TTDevice *device_;
};

}  // namespace tt::umd
