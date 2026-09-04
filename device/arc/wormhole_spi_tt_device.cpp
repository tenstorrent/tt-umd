/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/arc/wormhole_spi_tt_device.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include "spi_arc_command.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/tt_device/firmware/wormhole_device_firmware.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd {

// SPI Register Addresses (from Rust code).
static constexpr uint64_t GPIO2_PAD_TRIEN_CNTL = wormhole::ARC_RESET_UNIT_OFFSET + 0x240;
static constexpr uint64_t GPIO2_PAD_DRV_CNTL = wormhole::ARC_RESET_UNIT_OFFSET + 0x250;
static constexpr uint64_t GPIO2_PAD_RXEN_CNTL = wormhole::ARC_RESET_UNIT_OFFSET + 0x24C;
static constexpr uint64_t SPI_CNTL = wormhole::ARC_RESET_UNIT_OFFSET + 0xF8;

// SPI Controller Register Offsets (relative to SPI base).
static constexpr uint64_t SPI_BASE = 0x70000;
static constexpr uint64_t SPI_CTRLR0 = SPI_BASE + 0x00;
static constexpr uint64_t SPI_CTRLR1 = SPI_BASE + 0x04;
static constexpr uint64_t SPI_SSIENR = SPI_BASE + 0x08;
static constexpr uint64_t SPI_SER = SPI_BASE + 0x10;
static constexpr uint64_t SPI_BAUDR = SPI_BASE + 0x14;
static constexpr uint64_t SPI_SR = SPI_BASE + 0x28;
static constexpr uint64_t SPI_DR = SPI_BASE + 0x60;

// SPI Control Constants.
static constexpr uint32_t SPI_CNTL_SPI_ENABLE = 0x1;
static constexpr uint32_t SPI_CNTL_CLK_DISABLE = 0x1 << 8;
static constexpr uint32_t SPI_CNTL_SPI_DISABLE = 0x0;

static constexpr uint32_t SPI_SSIENR_ENABLE = 0x1;
static constexpr uint32_t SPI_SSIENR_DISABLE = 0x0;

static constexpr uint32_t SPI_CTRL0_TMOD_TRANSMIT_ONLY = 0x1 << 8;
static constexpr uint32_t SPI_CTRL0_TMOD_EEPROM_READ = 0x3 << 8;
static constexpr uint32_t SPI_CTRL0_SPI_FRF_STANDARD = 0x0 << 21;
static constexpr uint32_t SPI_CTRL0_DFS32_FRAME_08BITS = 0x7 << 16;

static constexpr uint32_t SPI_SR_RFNE = 0x1 << 3;
static constexpr uint32_t SPI_SR_TFE = 0x1 << 2;
static constexpr uint32_t SPI_SR_BUSY = 0x1 << 0;

// SPI Commands.
static constexpr uint8_t SPI_WR_EN_CMD = 0x06;
static constexpr uint8_t SPI_RD_STATUS_CMD = 0x05;
static constexpr uint8_t SPI_WR_STATUS_CMD = 0x01;

static constexpr uint32_t SPI_DUMP_ADDR_CORRECTION = 0x10000000;

static inline uint32_t spi_ctrl0_spi_scph(uint32_t scph) { return (scph << 6) & 0x1; }

static inline uint32_t spi_ctrl1_ndf(uint32_t frame_count) { return frame_count & 0xffff; }

static inline uint32_t spi_baudr_sckdv(uint32_t ssi_clk_div) { return ssi_clk_div & 0xffff; }

static inline uint32_t spi_ser_slave_disable(uint32_t slave_id) { return 0x0 << slave_id; }

static inline uint32_t spi_ser_slave_enable(uint32_t slave_id) { return 0x1 << slave_id; }

WormholeSPITTDevice::WormholeSPITTDevice(TTDevice* tt_device) :
    SPITTDevice(tt_device), firmware_(dynamic_cast<WormholeDeviceFirmware*>(tt_device->get_device_firmware())) {
    UMD_ASSERT(
        firmware_ != nullptr,
        error::RuntimeError,
        "WormholeSPITTDevice requires a device backed by WormholeDeviceFirmware.");
}

void WormholeSPITTDevice::get_aligned_params(
    uint32_t addr,
    uint32_t num_bytes,
    uint32_t chunk_size,
    uint32_t& start_addr,
    uint32_t& num_chunks,
    uint32_t& start_offset) {
    // Round down to the nearest chunk boundary.
    start_addr = (addr / chunk_size) * chunk_size;

    // Round up to the nearest chunk boundary.
    uint32_t end_addr = ((addr + num_bytes + chunk_size - 1) / chunk_size) * chunk_size;

    // Calculate number of chunks.
    num_chunks = (end_addr - start_addr) / chunk_size;

    // Calculate offset within the first chunk where actual data starts.
    start_offset = addr - start_addr;
}

uint32_t WormholeSPITTDevice::get_clock() {
    auto* telemetry = device_->get_firmware_telemetry_reader();
    uint32_t arcclk = 540;  // Default pessimistic value

    if (telemetry) {
        // TelemetryTag (unified enum) is only available in firmware >= 18.7
        // For older firmware, wormhole::LegacyTelemetryTag should be used.
        FirmwareBundleVersion fw_version = device_->get_firmware_version();

        if (fw_version < FirmwareBundleVersion(18, 7, 0)) {
            UMD_THROW(
                error::RuntimeError,
                "Firmware version " + fw_version.to_string() +
                    " is too old to support TelemetryTag::ARCCLK. Minimum required version is 18.7.0.");
        }

        try {
            arcclk = telemetry->read_entry(TelemetryTag::ARCCLK);
        } catch (...) {
            // If telemetry read fails, use default.
        }
    }

    uint32_t clock_div = static_cast<uint32_t>(std::ceil(arcclk / 20.0f));
    clock_div += clock_div % 2;  // Make it even

    return clock_div;
}

void WormholeSPITTDevice::init(uint32_t clock_div) {
    uint32_t reg;
    firmware_->read_from_arc_apb(&reg, GPIO2_PAD_TRIEN_CNTL, sizeof(reg), get_selected_noc_id());

    reg |= 1 << 2;     // Enable tristate for SPI data in PAD
    reg &= ~(1 << 5);  // Disable tristate for SPI chip select PAD
    reg &= ~(1 << 6);  // Disable tristate for SPI clock PAD
    firmware_->write_to_arc_apb(&reg, GPIO2_PAD_TRIEN_CNTL, sizeof(reg), get_selected_noc_id());

    uint32_t val = 0xffffffff;
    firmware_->write_to_arc_apb(&val, GPIO2_PAD_DRV_CNTL, sizeof(val), get_selected_noc_id());

    // Enable RX for all SPI PADS.
    firmware_->read_from_arc_apb(&reg, GPIO2_PAD_RXEN_CNTL, sizeof(reg), get_selected_noc_id());
    reg |= 0x3f << 1;  // PADs 1 to 6 are used for SPI quad SCPH support
    firmware_->write_to_arc_apb(&reg, GPIO2_PAD_RXEN_CNTL, sizeof(reg), get_selected_noc_id());

    val = SPI_CNTL_SPI_ENABLE;
    firmware_->write_to_arc_apb(&val, SPI_CNTL, sizeof(val), get_selected_noc_id());

    val = SPI_SSIENR_DISABLE;
    firmware_->write_to_arc_apb(&val, SPI_SSIENR, sizeof(val), get_selected_noc_id());

    val = SPI_CTRL0_TMOD_EEPROM_READ | SPI_CTRL0_SPI_FRF_STANDARD | SPI_CTRL0_DFS32_FRAME_08BITS |
          spi_ctrl0_spi_scph(0x1);
    firmware_->write_to_arc_apb(&val, SPI_CTRLR0, sizeof(val), get_selected_noc_id());

    val = 0;
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    val = spi_baudr_sckdv(clock_div);
    firmware_->write_to_arc_apb(&val, SPI_BAUDR, sizeof(val), get_selected_noc_id());

    val = SPI_SSIENR_ENABLE;
    firmware_->write_to_arc_apb(&val, SPI_SSIENR, sizeof(val), get_selected_noc_id());
}

void WormholeSPITTDevice::disable() {
    uint32_t val = SPI_CNTL_CLK_DISABLE | SPI_CNTL_SPI_DISABLE;
    firmware_->write_to_arc_apb(&val, SPI_CNTL, sizeof(val), get_selected_noc_id());
}

uint8_t WormholeSPITTDevice::read_status(uint8_t register_addr) {
    uint32_t val;

    val = SPI_SSIENR_DISABLE;
    firmware_->write_to_arc_apb(&val, SPI_SSIENR, sizeof(val), get_selected_noc_id());

    val = SPI_CTRL0_TMOD_EEPROM_READ | SPI_CTRL0_SPI_FRF_STANDARD | SPI_CTRL0_DFS32_FRAME_08BITS |
          spi_ctrl0_spi_scph(0x1);
    firmware_->write_to_arc_apb(&val, SPI_CTRLR0, sizeof(val), get_selected_noc_id());

    val = spi_ctrl1_ndf(0);
    firmware_->write_to_arc_apb(&val, SPI_CTRLR1, sizeof(val), get_selected_noc_id());

    val = SPI_SSIENR_ENABLE;
    firmware_->write_to_arc_apb(&val, SPI_SSIENR, sizeof(val), get_selected_noc_id());

    val = spi_ser_slave_disable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Write status register to read.
    val = register_addr;
    firmware_->write_to_arc_apb(&val, SPI_DR, sizeof(val), get_selected_noc_id());

    val = spi_ser_slave_enable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Wait for data to be available.
    do {
        firmware_->read_from_arc_apb(&val, SPI_SR, sizeof(val), get_selected_noc_id());
    } while ((val & SPI_SR_RFNE) == 0);

    firmware_->read_from_arc_apb(&val, SPI_DR, sizeof(val), get_selected_noc_id());
    uint8_t read_buf = val & 0xff;

    val = spi_ser_slave_disable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    return read_buf;
}

void WormholeSPITTDevice::lock(uint8_t sections) {
    uint32_t val;

    // Set slave address.
    val = SPI_SSIENR_DISABLE;
    firmware_->write_to_arc_apb(&val, SPI_SSIENR, sizeof(val), get_selected_noc_id());

    val = SPI_CTRL0_TMOD_TRANSMIT_ONLY | SPI_CTRL0_SPI_FRF_STANDARD | SPI_CTRL0_DFS32_FRAME_08BITS |
          spi_ctrl0_spi_scph(0x1);
    firmware_->write_to_arc_apb(&val, SPI_CTRLR0, sizeof(val), get_selected_noc_id());

    val = SPI_SSIENR_ENABLE;
    firmware_->write_to_arc_apb(&val, SPI_SSIENR, sizeof(val), get_selected_noc_id());

    val = spi_ser_slave_disable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Enable write.
    val = SPI_WR_EN_CMD;
    firmware_->write_to_arc_apb(&val, SPI_DR, sizeof(val), get_selected_noc_id());

    val = spi_ser_slave_enable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Wait for TX FIFO empty.
    do {
        firmware_->read_from_arc_apb(&val, SPI_SR, sizeof(val), get_selected_noc_id());
    } while ((val & SPI_SR_TFE) != SPI_SR_TFE);

    // Wait for not busy.
    do {
        firmware_->read_from_arc_apb(&val, SPI_SR, sizeof(val), get_selected_noc_id());
    } while ((val & SPI_SR_BUSY) == SPI_SR_BUSY);

    val = spi_ser_slave_disable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Write sectors to lock.
    val = SPI_WR_STATUS_CMD;
    firmware_->write_to_arc_apb(&val, SPI_DR, sizeof(val), get_selected_noc_id());

    // Determine board type to figure out which SPI to use.
    uint64_t board_id = device_->get_board_id();
    uint32_t upi = (board_id >> (32 + 4)) & 0xFFFFF;
    bool simple_spi = (upi == 0x35);

    // Write sector lock info.
    if (simple_spi) {
        val = (1 << 6) | (static_cast<uint32_t>(sections) << 2);
    } else if (sections < 5) {
        val = (0x3 << 5) | (static_cast<uint32_t>(sections) << 2);
    } else {
        val = (0x1 << 5) | ((static_cast<uint32_t>(sections) - 5) << 2);
    }
    firmware_->write_to_arc_apb(&val, SPI_DR, sizeof(val), get_selected_noc_id());

    val = spi_ser_slave_enable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Wait for TX FIFO empty.
    do {
        firmware_->read_from_arc_apb(&val, SPI_SR, sizeof(val), get_selected_noc_id());
    } while ((val & SPI_SR_TFE) != SPI_SR_TFE);

    // Wait for not busy.
    do {
        firmware_->read_from_arc_apb(&val, SPI_SR, sizeof(val), get_selected_noc_id());
    } while ((val & SPI_SR_BUSY) == SPI_SR_BUSY);

    val = spi_ser_slave_disable(0);
    firmware_->write_to_arc_apb(&val, SPI_SER, sizeof(val), get_selected_noc_id());

    // Wait for lock operation to complete.
    while ((read_status(SPI_RD_STATUS_CMD) & 0x1) == 0x1) {
    }
}

void WormholeSPITTDevice::unlock() {
    lock(0);  // Unlocking is just locking with 0 sections
}

void WormholeSPITTDevice::read(uint32_t addr, uint8_t* data, size_t size) {
    if (addr + size > wormhole::SPI_ROM_SIZE) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("SPI read out of bounds: {:#x} + {} > {:#x}", addr, size, wormhole::SPI_ROM_SIZE));
    }
    if (size == 0) {
        return;
    }

    auto* firmware = device_->get_device_firmware();
    if (!firmware) {
        UMD_THROW(error::RuntimeError, "Device firmware not available for SPI read on Wormhole.");
    }

    std::vector<uint32_t> ret(1);
    uint32_t rc = send_spi_arc_command(
        firmware,
        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::GET_SPI_DUMP_ADDR),
        ret);
    if (rc != 0 || ret.empty()) {
        UMD_THROW(error::RuntimeError, "Failed to get SPI dump address on Wormhole.");
    }

    uint32_t spi_dump_addr_offset = ret[0];
    uint64_t spi_dump_addr = wormhole::ARC_CSM_OFFSET_NOC + (spi_dump_addr_offset - SPI_DUMP_ADDR_CORRECTION);

    // Get aligned parameters.
    uint32_t start_addr;
    uint32_t num_chunks;
    uint32_t start_offset;
    get_aligned_params(addr, size, wormhole::ARC_SPI_CHUNK_SIZE, start_addr, num_chunks, start_offset);

    std::vector<uint8_t> chunk_buf(wormhole::ARC_SPI_CHUNK_SIZE);

    for (uint32_t chunk = 0; chunk < num_chunks; ++chunk) {
        uint32_t offset = chunk * wormhole::ARC_SPI_CHUNK_SIZE;
        uint32_t chunk_addr = start_addr + offset;

        uint32_t spi_read_msg =
            wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::SPI_READ);
        send_spi_arc_command(firmware, spi_read_msg, ret, {chunk_addr & 0xFFFF, (chunk_addr >> 16) & 0xFFFF});
        device_->read_from_device(
            chunk_buf.data(),
            device_->get_arc_core(),
            spi_dump_addr,
            wormhole::ARC_SPI_CHUNK_SIZE,
            get_selected_noc_id());

        // Copy the relevant portion of the chunk to the output buffer.
        if (offset < start_offset) {
            // First chunk: skip the beginning.
            uint32_t skip = start_offset - offset;
            uint32_t copy_size = std::min<uint32_t>(wormhole::ARC_SPI_CHUNK_SIZE - skip, size);
            std::memcpy(data, chunk_buf.data() + skip, copy_size);
        } else {
            // Subsequent chunks.
            uint32_t output_offset = offset - start_offset;
            uint32_t copy_size = std::min<uint32_t>(wormhole::ARC_SPI_CHUNK_SIZE, size - output_offset);
            std::memcpy(data + output_offset, chunk_buf.data(), copy_size);
        }
    }
}

void WormholeSPITTDevice::write(uint32_t addr, const uint8_t* data, size_t size, bool skip_write_to_spi) {
    if (size == 0) {
        return;
    }

    auto* firmware = device_->get_device_firmware();
    if (!firmware) {
        UMD_THROW(error::RuntimeError, "Device firmware not available for SPI write on Wormhole.");
    }

    uint32_t clock_div = get_clock();

    // Must call init before unlock.
    init(clock_div);
    unlock();
    // Technically we would save a write by not calling disable here, however in the case where
    // we are using the arc messages the ARC code will call disable requiring another init. It
    // feels a bit safer therefore to always init before each read/write step.
    disable();

    // Perform the actual write operation.
    std::exception_ptr write_exception;
    try {
        std::vector<uint32_t> ret(1);
        uint32_t rc = send_spi_arc_command(
            firmware,
            wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::GET_SPI_DUMP_ADDR),
            ret);
        if (rc != 0 || ret.empty()) {
            UMD_THROW(error::RuntimeError, "Failed to get SPI dump address on Wormhole.");
        }

        uint32_t spi_dump_addr_offset = ret[0];
        uint64_t spi_dump_addr = wormhole::ARC_CSM_OFFSET_NOC + (spi_dump_addr_offset - SPI_DUMP_ADDR_CORRECTION);

        // Get aligned parameters.
        uint32_t start_addr;
        uint32_t num_chunks;
        uint32_t start_offset;
        get_aligned_params(addr, size, wormhole::ARC_SPI_CHUNK_SIZE, start_addr, num_chunks, start_offset);

        std::vector<uint8_t> chunk_buf(wormhole::ARC_SPI_CHUNK_SIZE);

        for (uint32_t chunk = 0; chunk < num_chunks; ++chunk) {
            uint32_t offset = chunk * wormhole::ARC_SPI_CHUNK_SIZE;
            uint32_t chunk_addr = start_addr + offset;

            // Read the current chunk first.
            uint32_t spi_read_msg =
                wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::SPI_READ);
            send_spi_arc_command(firmware, spi_read_msg, ret, {chunk_addr & 0xFFFF, (chunk_addr >> 16) & 0xFFFF});

            device_->read_from_device(
                chunk_buf.data(),
                device_->get_arc_core(),
                spi_dump_addr,
                wormhole::ARC_SPI_CHUNK_SIZE,
                get_selected_noc_id());

            // Keep a copy to check if we need to write.
            std::vector<uint8_t> orig_data = chunk_buf;

            // Modify the relevant portion with new data.
            if (offset < start_offset) {
                // First chunk: skip the beginning.
                uint32_t skip = start_offset - offset;
                uint32_t copy_size = std::min<uint32_t>(wormhole::ARC_SPI_CHUNK_SIZE - skip, size);
                std::memcpy(chunk_buf.data() + skip, data, copy_size);
            } else {
                // Subsequent chunks.
                uint32_t input_offset = offset - start_offset;
                uint32_t copy_size = std::min<uint32_t>(wormhole::ARC_SPI_CHUNK_SIZE, size - input_offset);
                std::memcpy(chunk_buf.data(), data + input_offset, copy_size);
            }

            // Only write if the data changed.
            if (chunk_buf != orig_data) {
                device_->write_to_device(
                    chunk_buf.data(),
                    device_->get_arc_core(),
                    spi_dump_addr,
                    wormhole::ARC_SPI_CHUNK_SIZE,
                    get_selected_noc_id());

                if (!skip_write_to_spi) {
                    uint32_t spi_write_msg =
                        wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::SPI_WRITE);
                    send_spi_arc_command(firmware, spi_write_msg, ret, {0xFFFF, 0xFFFF});
                }
            }
        }
    } catch (...) {
        write_exception = std::current_exception();
    }

    // Always try to lock, even if write failed.
    std::exception_ptr lock_exception;
    try {
        init(clock_div);
        lock(8);  // Lock with 8 sections
        disable();
    } catch (...) {
        lock_exception = std::current_exception();
    }

    // Rethrow write exception if it occurred.
    if (write_exception) {
        std::rethrow_exception(write_exception);
    }

    // Rethrow lock exception if write succeeded but lock failed.
    if (lock_exception) {
        std::rethrow_exception(lock_exception);
    }
}

}  // namespace tt::umd
