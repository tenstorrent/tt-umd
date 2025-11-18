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

// Template member function implementation
template <typename Reader>
std::optional<TtBootFsFd> BlackholeSPI::find_boot_fs_tag(Reader&& reader, const std::string& tag_name) {
    uint32_t curr_addr = 0;
    uint32_t entry_count = 0;

    while (true) {
        if (entry_count >= 1000) {
            std::cout << "Safety exit, tag not found" << std::endl;
            return std::nullopt;
        }

        TtBootFsFd fd{};
        reader(curr_addr, sizeof(TtBootFsFd), reinterpret_cast<uint8_t*>(&fd));

        if (fd.flags.invalid()) {
            std::cout << "Found invalid entry (end of table), tag not found" << std::endl;
            return std::nullopt;
        }

        std::string current_tag = fd.image_tag_str();
        if (current_tag == tag_name) {
            return fd;
        }

        curr_addr += sizeof(TtBootFsFd);
        entry_count++;
    }
}

std::optional<uint32_t> BlackholeSPI::extract_protobuf_uint32_field(
    const uint8_t* data, size_t size, uint32_t field_number) {
    size_t pos = 0;
    uint32_t target_key = (field_number << 3) | 0;  // field_number with wire type 0 (varint)

    while (pos < size) {
        // Read key (field number + wire type)
        if (pos >= size) {
            break;
        }
        uint32_t key = data[pos++];

        // Handle multi-byte varint keys (unlikely for small field numbers)
        if (key & 0x80) {
            key = (key & 0x7F);
            if (pos >= size) {
                break;
            }
            key |= (data[pos++] & 0x7F) << 7;
        }

        uint32_t wire_type = key & 0x7;
        uint32_t field_num = key >> 3;

        if (field_num == field_number && wire_type == 0) {
            // Found our field - read the varint value
            uint32_t value = 0;
            int shift = 0;
            while (pos < size && shift < 32) {
                uint8_t byte = data[pos++];
                value |= static_cast<uint32_t>(byte & 0x7F) << shift;
                if (!(byte & 0x80)) {
                    return value;
                }
                shift += 7;
            }
            return std::nullopt;  // Incomplete varint
        }

        // Skip this field based on wire type
        switch (wire_type) {
            case 0:  // Varint
                while (pos < size && (data[pos] & 0x80)) {
                    pos++;
                }
                pos++;  // Skip last byte
                break;
            case 1:  // 64-bit
                pos += 8;
                break;
            case 2: {  // Length-delimited
                if (pos >= size) {
                    return std::nullopt;
                }
                uint32_t length = data[pos++];
                pos += length;
                break;
            }
            case 5:  // 32-bit
                pos += 4;
                break;
            default:
                return std::nullopt;  // Unknown wire type
        }
    }

    return std::nullopt;
}

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

uint32_t BlackholeSPI::get_spi_fw_bundle_version() {
    // Read the cmfwcfg boot FS entry from SPI
    auto reader = [this](uint32_t addr, size_t size, uint8_t* buffer) { this->read(addr, buffer, size); };

    auto cmfwcfg_fd = find_boot_fs_tag(reader, "cmfwcfg");

    if (!cmfwcfg_fd.has_value()) {
        throw std::runtime_error("cmfwcfg tag not found in boot FS table");
    }

    // Read the protobuf data from SPI
    uint32_t proto_size = cmfwcfg_fd->flags.image_size();

    if (proto_size == 0 || proto_size > 1024 * 1024) {  // Sanity check: max 1MB
        throw std::runtime_error("Invalid cmfwcfg size: " + std::to_string(proto_size));
    }

    std::vector<uint8_t> proto_data(proto_size);
    this->read(cmfwcfg_fd->spi_addr, proto_data.data(), proto_size);

    // Remove padding from protobuf data
    // Last byte indicates padding length
    if (proto_data.empty()) {
        throw std::runtime_error("Empty cmfwcfg data");
    }

    uint8_t last_byte = proto_data[proto_data.size() - 1];
    size_t actual_size = proto_data.size() - last_byte - 1;

    // Extract fw_bundle_version field from protobuf
    // Field number 1 corresponds to fw_bundle_version in the FwTable protobuf definition
    auto fw_bundle_version = extract_protobuf_uint32_field(proto_data.data(), actual_size, 1);
    if (!fw_bundle_version.has_value()) {
        throw std::runtime_error("fw_bundle_version field not found in cmfwcfg protobuf");
    }

    return *fw_bundle_version;
}

}  // namespace tt::umd
