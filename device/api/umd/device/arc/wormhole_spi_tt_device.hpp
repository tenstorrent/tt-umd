/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/arc/spi_tt_device.hpp"

namespace tt::umd {

class TTDevice;

/**
 * Wormhole-specific SPI implementation.
 * Uses aligned chunk-based reading/writing with the ARC messenger.
 */
class WormholeSPITTDevice : public SPITTDevice {
public:
    explicit WormholeSPITTDevice(TTDevice* tt_device);

    void read(uint32_t addr, uint8_t* data, size_t size) override;
    void write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi = false) override;

private:
    /**
     * Helper function to calculate aligned parameters for SPI read/write operations.
     * SPI operations must be aligned to chunk boundaries.
     *
     * @param addr Starting address for the SPI operation
     * @param size Number of bytes to read/write
     * @param chunk_size Size of each chunk (e.g., ARC_SPI_CHUNK_SIZE)
     * @param start_addr Output: aligned start address (rounded down to chunk boundary)
     * @param num_chunks Output: number of chunks to process
     * @param start_offset Output: offset within first chunk where actual data starts
     */
    static void get_aligned_params(
        uint32_t addr,
        uint32_t size,
        uint32_t chunk_size,
        uint32_t& start_addr,
        uint32_t& num_chunks,
        uint32_t& start_offset);

    // SPI hardware control functions (only used for write operations).
    uint32_t get_clock();
    void init(uint32_t clock_div);
    void disable();
    void unlock();
    void lock(uint8_t sections);
    uint8_t read_status(uint8_t register_addr);
};

}  // namespace tt::umd
