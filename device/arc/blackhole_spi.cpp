/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/arc/blackhole_spi.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"

namespace tt::umd {

BlackholeSPI::BlackholeSPI(TTDevice* tt_device) : tt_device_(tt_device) {}

void BlackholeSPI::read(uint32_t addr, uint8_t* data, size_t size) {
    if (size == 0) {
        return;
    }

    auto* messenger = tt_device_->get_arc_messenger();
    if (!messenger) {
        throw std::runtime_error("ARC messenger not available for SPI read on Blackhole.");
    }

    // Get SPI buffer info from SCRATCH_RAM[10]
    // The Rust code reads from spi_buffer_addr which is arc_ss.reset_unit.SCRATCH_RAM[10]
    uint32_t buffer_info;
    tt_device_->read_from_arc_apb(&buffer_info, blackhole::SCRATCH_RAM_10, sizeof(buffer_info));

    // Parse buffer info: lower 24 bits = address offset, upper 8 bits = size (as power of 2)
    uint32_t buffer_addr = (buffer_info & 0xFFFFFF) + 0x10000000;  // magic offset to translate
    uint32_t buffer_size = 1 << ((buffer_info >> 24) & 0xFF);

    size_t bytes_read = 0;
    while (bytes_read < size) {
        uint32_t chunk_addr = addr + bytes_read;
        uint32_t chunk_size = std::min<uint32_t>(static_cast<uint32_t>(size - bytes_read), buffer_size);

        // Request ARC to read chunk into dump buffer using READ_EEPROM (0x19)
        std::vector<uint32_t> read_ret;
        uint32_t rc = messenger->send_message(
            static_cast<uint32_t>(blackhole::ArcMessageType::READ_EEPROM),
            read_ret,
            {chunk_addr, chunk_size, buffer_addr});

        if (rc != 0) {
            throw std::runtime_error("Failed to read from SPI on Blackhole.");
        }

        // Read data from buffer
        tt_device_->read_from_device(data + bytes_read, tt_device_->get_arc_core(), buffer_addr, chunk_size);
        bytes_read += chunk_size;
    }
}

void BlackholeSPI::write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi) {
    if (size == 0) {
        return;
    }

    auto* messenger = tt_device_->get_arc_messenger();
    if (!messenger) {
        throw std::runtime_error("ARC messenger not available for SPI write on Blackhole.");
    }

    // Get SPI buffer info from SCRATCH_RAM[10]
    // The Rust code reads from spi_buffer_addr which is arc_ss.reset_unit.SCRATCH_RAM[10]
    uint32_t buffer_info;
    tt_device_->read_from_arc_apb(&buffer_info, blackhole::SCRATCH_RAM_10, sizeof(buffer_info));

    // Parse buffer info: lower 24 bits = address offset, upper 8 bits = size (as power of 2)
    uint32_t buffer_addr = (buffer_info & 0xFFFFFF) + 0x10000000;  // magic offset to translate
    uint32_t buffer_size = 1 << ((buffer_info >> 24) & 0xFF);

    size_t bytes_written = 0;
    while (bytes_written < size) {
        uint32_t chunk_addr = addr + bytes_written;
        uint32_t chunk_size = std::min<uint32_t>(static_cast<uint32_t>(size - bytes_written), buffer_size);

        // Write data to buffer first
        tt_device_->write_to_device(data + bytes_written, tt_device_->get_arc_core(), buffer_addr, chunk_size);

        if (!skip_write_to_spi) {
            // Request ARC to write chunk from buffer to SPI using WRITE_EEPROM (0x1A)
            std::vector<uint32_t> write_ret;
            uint32_t rc = messenger->send_message(
                static_cast<uint32_t>(blackhole::ArcMessageType::WRITE_EEPROM),
                write_ret,
                {chunk_addr, chunk_size, buffer_addr});

            // Sleep briefly to allow the write to complete (as in Rust implementation)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (rc != 0) {
                throw std::runtime_error("Failed to write to SPI on Blackhole.");
            }
        }

        bytes_written += chunk_size;
    }
}

}  // namespace tt::umd
