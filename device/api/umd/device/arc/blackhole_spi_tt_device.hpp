/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "umd/device/arc/spi_tt_device.hpp"

namespace tt::umd {

class TTDevice;

// Boot filesystem structures and constants.
constexpr size_t IMAGE_TAG_SIZE = 8;

/**
 * Boot filesystem file descriptor flags.
 * Packed bitfield matching Rust implementation.
 */
struct FdFlags {
    uint32_t value;

    uint32_t image_size() const { return value & 0xFFFFFF; }  // bits 0-23

    bool invalid() const { return (value >> 24) & 0x1; }  // bit 24

    bool executable() const { return (value >> 25) & 0x1; }  // bit 25
};

/**
 * Boot filesystem file descriptor.
 * Must match the layout in SPI flash (little-endian).
 */
struct TtBootFsFd {
    uint32_t spi_addr;
    uint32_t copy_dest;
    FdFlags flags;
    uint32_t data_crc;
    uint32_t security_flags;
    uint8_t image_tag[IMAGE_TAG_SIZE];
    uint32_t fd_crc;

    std::string image_tag_str() const {
        size_t len = 0;
        while (len < IMAGE_TAG_SIZE && image_tag[len] != 0) {
            len++;
        }
        return std::string(reinterpret_cast<const char*>(image_tag), len);
    }
};

static_assert(sizeof(TtBootFsFd) == 32, "TtBootFsFd must be 32 bytes");

/**
 * Blackhole-specific SPI implementation.
 * Uses dynamic buffer info from SCRATCH_RAM and EEPROM ARC messages.
 */
class BlackholeSPITTDevice : public SPITTDevice {
public:
    explicit BlackholeSPITTDevice(TTDevice* tt_device);

    void read(uint32_t addr, uint8_t* data, size_t size) override;
    void write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi = false) override;

    /**
     * Get the firmware bundle version by reading from SPI flash.
     *
     * This function:
     * 1. Scans the boot filesystem table in SPI starting at address 0
     * 2. Finds the "cmfwcfg" entry
     * 3. Reads and parses the protobuf data
     * 4. Extracts the fw_bundle_version field (field 1 in FwTable proto)
     *
     * The version is encoded in the 32-bit protobuf value as:
     * - Byte 0 (bits 0-7): patch
     * - Byte 1 (bits 8-15): minor
     * - Byte 2 (bits 16-23): major
     * - Byte 3 (bits 24-31): component
     *
     * @return The raw 32-bit firmware bundle version value
     * @throws std::runtime_error if cmfwcfg not found or cannot be parsed
     */
    uint32_t get_spi_fw_bundle_version() override;

private:
    /**
     * Find a boot filesystem entry by tag name.
     *
     * @param reader Function to read data from SPI: reader(addr, size, buffer)
     * @param tag_name The tag to search for (e.g., "cmfwcfg")
     * @return The file descriptor if found, nullopt otherwise
     */
    template <typename Reader>
    std::optional<TtBootFsFd> find_boot_fs_tag(const Reader& reader, const std::string& tag_name);

    /**
     * Extract a protobuf varint field value from raw protobuf data.
     * This is a simple parser for extracting uint32 fields.
     *
     * @param data Protobuf data
     * @param size Size of data
     * @param field_number The protobuf field number to extract
     * @return The field value if found, nullopt otherwise
     */
    std::optional<uint32_t> extract_protobuf_uint32_field(const uint8_t* data, size_t size, uint32_t field_number);
};

}  // namespace tt::umd
