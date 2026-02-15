/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/arc/blackhole_spi_tt_device.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "umd/device/arc/arc_messenger.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

namespace {

// Reads a protobuf varint from data at pos, advances pos. Returns nullopt if incomplete or out of bounds.
std::optional<uint32_t> read_varint(const uint8_t* data, size_t size, size_t& pos) {
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
    return std::nullopt;
}

}  // namespace

// Firmware version from which Blackhole SPI write requires unlock (0xC2) before and lock (0xC3) after.
static const semver_t BH_SPI_LOCK_REQUIRED_SINCE_FW(19, 0, 0);

// Maximum boot filesystem entries to scan; safety limit to avoid infinite loop on corrupted table.
constexpr uint32_t BOOT_FS_MAX_ENTRIES_SCAN = 1000;

// SCRATCH_RAM[10] buffer info layout: lower 24 bits = address offset, upper 8 bits = size (power of 2).
constexpr uint32_t BH_SPI_ADDR_MASK_24_BITS = 0xFFFFFF;
constexpr uint32_t BH_SPI_ARC_ADDR_OFFSET = 0x10000000;
constexpr unsigned BH_SPI_SIZE_SHIFT_BITS = 24;
constexpr uint32_t BH_SPI_SIZE_MASK_8_BITS = 0xFF;

struct SpiBufferInfo {
    uint32_t addr;
    uint32_t size;
};

// Reads SPI dump buffer address and size from SCRATCH_RAM[10]. addr is ARC physical address (bytes),
// size is buffer size in bytes (2^upper_8_bits from register).
static SpiBufferInfo get_spi_buffer_info(TTDevice* device) {
    uint32_t buffer_info;
    device->read_from_arc_apb(&buffer_info, blackhole::SCRATCH_RAM_10, sizeof(buffer_info));
    uint32_t buffer_addr = (buffer_info & BH_SPI_ADDR_MASK_24_BITS) + BH_SPI_ARC_ADDR_OFFSET;
    uint32_t buffer_size = 1u << ((buffer_info >> BH_SPI_SIZE_SHIFT_BITS) & BH_SPI_SIZE_MASK_8_BITS);
    return {buffer_addr, buffer_size};
}

// Template member function implementation.
template <typename Reader>
std::optional<TtBootFsFd> BlackholeSPITTDevice::find_boot_fs_tag(const Reader& reader, const std::string& tag_name) {
    uint32_t curr_addr = 0;
    uint32_t entry_count = 0;

    while (true) {
        if (entry_count >= BOOT_FS_MAX_ENTRIES_SCAN) {
            std::cout << "Safety exit, tag not found" << std::endl;
            return std::nullopt;
        }

        TtBootFsFd fd{};
        std::invoke(reader, curr_addr, sizeof(TtBootFsFd), reinterpret_cast<uint8_t*>(&fd));

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

std::optional<uint32_t> BlackholeSPITTDevice::extract_protobuf_uint32_field(
    const uint8_t* data, size_t size, uint32_t field_number) {
    size_t pos = 0;

    while (pos < size) {
        std::optional<uint32_t> key_opt = read_varint(data, size, pos);
        if (!key_opt.has_value()) {
            break;
        }
        uint32_t key = *key_opt;
        uint32_t wire_type = key & 0x7;
        uint32_t field_num = key >> 3;

        if (field_num == field_number && wire_type == 0) {
            return read_varint(data, size, pos);
        }

        // Skip this field based on wire type.
        switch (wire_type) {
            case 0:  // Varint
                if (!read_varint(data, size, pos).has_value()) {
                    return std::nullopt;
                }
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

BlackholeSPITTDevice::BlackholeSPITTDevice(TTDevice* tt_device) : SPITTDevice(tt_device) {}

void BlackholeSPITTDevice::read(uint32_t addr, uint8_t* data, size_t size) {
    if (size == 0) {
        return;
    }

    auto* messenger = device_->get_arc_messenger();
    if (!messenger) {
        throw std::runtime_error("ARC messenger not available for SPI read on Blackhole.");
    }

    auto [buffer_addr, buffer_size] = get_spi_buffer_info(device_);

    size_t bytes_read = 0;
    while (bytes_read < size) {
        size_t remaining = size - bytes_read;
        uint32_t chunk_addr = addr + bytes_read;
        uint32_t chunk_size = std::min<uint32_t>(static_cast<uint32_t>(remaining), buffer_size);

        // Request ARC to read chunk into dump buffer using READ_EEPROM (0x19).
        std::vector<uint32_t> read_ret;
        uint32_t rc = messenger->send_message(
            static_cast<uint32_t>(blackhole::ArcMessageType::READ_EEPROM),
            read_ret,
            {chunk_addr, chunk_size, buffer_addr});

        if (rc != 0) {
            throw std::runtime_error("Failed to read from SPI on Blackhole.");
        }

        // Read data from buffer.
        device_->read_from_device(data + bytes_read, device_->get_arc_core(), buffer_addr, chunk_size);
        bytes_read += chunk_size;
        // Guard against bytes_read exceeding size (e.g. if device returned more than requested).
        bytes_read = std::min(bytes_read, size);
    }
}

void BlackholeSPITTDevice::write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi) {
    if (size == 0) {
        return;
    }

    auto* messenger = device_->get_arc_messenger();
    if (!messenger) {
        throw std::runtime_error("ARC messenger not available for SPI write on Blackhole.");
    }

    auto [buffer_addr, buffer_size] = get_spi_buffer_info(device_);

    // Since BH_SPI_LOCK_REQUIRED_SINCE_FW, SPI write must be accompanied by unlock (0xC2) before and lock (0xC3) after.
    semver_t fw_version = device_->get_firmware_version();
    const bool need_lock_unlock =
        (semver_t::compare_firmware_bundle(fw_version, BH_SPI_LOCK_REQUIRED_SINCE_FW) >= 0) && !skip_write_to_spi;

    if (need_lock_unlock) {
        std::vector<uint32_t> unlock_ret;
        uint32_t rc =
            messenger->send_message(static_cast<uint32_t>(blackhole::ArcMessageType::SPI_UNLOCK), unlock_ret, {});
        if (rc != 0) {
            throw std::runtime_error("Failed to unlock SPI for write on Blackhole (fw >= 19.0).");
        }
    }

    size_t bytes_written = 0;
    while (bytes_written < size) {
        size_t remaining = size - bytes_written;
        uint32_t chunk_addr = addr + bytes_written;
        uint32_t chunk_size = std::min<uint32_t>(static_cast<uint32_t>(remaining), buffer_size);

        // Write data to buffer first.
        device_->write_to_device(data + bytes_written, device_->get_arc_core(), buffer_addr, chunk_size);

        if (!skip_write_to_spi) {
            // Request ARC to write chunk from buffer to SPI using WRITE_EEPROM (0x1A).
            std::vector<uint32_t> write_ret;
            uint32_t rc = messenger->send_message(
                static_cast<uint32_t>(blackhole::ArcMessageType::WRITE_EEPROM),
                write_ret,
                {chunk_addr, chunk_size, buffer_addr});

            // Sleep briefly to allow the write to complete (as in Rust implementation).
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (rc != 0) {
                // Relock SPI if unlock was successful.
                if (need_lock_unlock) {
                    std::vector<uint32_t> lock_ret;
                    messenger->send_message(static_cast<uint32_t>(blackhole::ArcMessageType::SPI_LOCK), lock_ret, {});
                }
                throw std::runtime_error("Failed to write to SPI on Blackhole.");
            }
        }

        bytes_written += chunk_size;
        bytes_written = std::min(bytes_written, size);
    }

    if (need_lock_unlock) {
        std::vector<uint32_t> lock_ret;
        uint32_t rc = messenger->send_message(static_cast<uint32_t>(blackhole::ArcMessageType::SPI_LOCK), lock_ret, {});
        if (rc != 0) {
            throw std::runtime_error("Failed to lock SPI after write on Blackhole (fw >= 19.0).");
        }
    }
}

uint32_t BlackholeSPITTDevice::get_spi_fw_bundle_version() {
    // Read the cmfwcfg boot FS entry from SPI.
    auto reader = [this](uint32_t addr, size_t size, uint8_t* buffer) { this->read(addr, buffer, size); };

    auto cmfwcfg_fd = find_boot_fs_tag(reader, "cmfwcfg");

    if (!cmfwcfg_fd.has_value()) {
        throw std::runtime_error("cmfwcfg tag not found in boot FS table");
    }

    // Read the protobuf data from SPI.
    uint32_t proto_size = cmfwcfg_fd->flags.image_size();

    if (proto_size == 0 || proto_size > 1024 * 1024) {  // Sanity check: max 1MB
        throw std::runtime_error("Invalid cmfwcfg size: " + std::to_string(proto_size));
    }

    std::vector<uint8_t> proto_data(proto_size);
    this->read(cmfwcfg_fd->spi_addr, proto_data.data(), proto_size);

    // Remove padding from protobuf data
    // Last byte indicates padding length.
    if (proto_data.empty()) {
        throw std::runtime_error("Empty cmfwcfg data");
    }

    uint8_t last_byte = proto_data[proto_data.size() - 1];
    size_t actual_size = proto_data.size() - last_byte - 1;

    // Extract fw_bundle_version field from protobuf
    // Field number 1 corresponds to fw_bundle_version in the FwTable protobuf definition.
    auto fw_bundle_version = extract_protobuf_uint32_field(proto_data.data(), actual_size, 1);
    if (!fw_bundle_version.has_value()) {
        throw std::runtime_error("fw_bundle_version field not found in cmfwcfg protobuf");
    }

    return *fw_bundle_version;
}

}  // namespace tt::umd
