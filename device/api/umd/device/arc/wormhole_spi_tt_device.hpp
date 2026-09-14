/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/arc/spi_tt_device.hpp"

namespace tt::umd {

class DeviceProtocol;
class WormholeDeviceFirmware;

/**
 * Wormhole-specific SPI implementation.
 * Uses aligned chunk-based reading/writing with the ARC messenger.
 */
class WormholeSPITTDevice : public SPITTDevice {
public:
    /**
     * @param protocol Protocol to issue the ARC dump-buffer accesses through.
     * @param firmware The device's firmware component. Must not be null; SPITTDevice::create resolves it.
     */
    WormholeSPITTDevice(DeviceProtocol* protocol, WormholeDeviceFirmware* firmware);

    void read(uint32_t addr, uint8_t* data, size_t size) override;
    void write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi = false) override;

private:
    /**
     * Helper function to calculate aligned parameters for SPI read/write operations.
     * SPI operations must be aligned to chunk boundaries.
     *
     * @param addr Starting address for the SPI operation
     * @param num_bytes Number of bytes to read/write
     * @param chunk_size Size of each chunk (e.g., ARC_SPI_CHUNK_SIZE)
     * @param start_addr Output: aligned start address (rounded down to chunk boundary)
     * @param num_chunks Output: number of chunks to process
     * @param start_offset Output: offset within first chunk where actual data starts
     */
    static void get_aligned_params(
        uint32_t addr,
        uint32_t num_bytes,
        uint32_t chunk_size,
        uint32_t& start_addr,
        uint32_t& num_chunks,
        uint32_t& start_offset);

    // SPI hardware control functions (only used for write operations).

    // Get the clock frequency of the device in MHz.
    uint32_t get_clock();

    void init(uint32_t clock_div);
    void disable();
    void unlock();
    void lock(uint8_t sections);
    uint8_t read_status(uint8_t register_addr);

    // Data accesses to the ARC core's SPI dump buffer, routed on the thread-selected NOC.
    void read_from_arc(void* dst, uint64_t addr, size_t size);
    void write_to_arc(const void* src, uint64_t addr, size_t size);

    // SPI control registers live behind the ARC APB window, which is an implementation detail of the
    // firmware component rather than part of its interface, so the concrete type is held here.
    WormholeDeviceFirmware* firmware_;
};

}  // namespace tt::umd
