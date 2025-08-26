// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/jtag/jtag_device.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/jtag/jtag.h"

constexpr uint32_t ROW_LEN = 12;
constexpr uint32_t WORMHOLE_ID = 0x138a5;
constexpr uint32_t WORMHOLE_ARC_EFUSE_BOX1 = 0x80042000;
constexpr uint32_t WORMHOLE_ARC_EFUSE_HARVESTING = (WORMHOLE_ARC_EFUSE_BOX1 + 0x25C);

/* static */ std::filesystem::path JtagDevice::jtag_library_path = std::filesystem::path("./build/lib/lib_tt_jtag.so");

JtagDevice::JtagDevice(std::unique_ptr<Jtag> jtag_device) : jtag(std::move(jtag_device)) {
    jtag->close_jlink();

    std::vector<uint32_t> potential_devices = jtag->enumerate_jlink();
    if (potential_devices.empty()) {
        TT_THROW("There are no devices");
    }

    for (int jlink_id : potential_devices) {
        uint32_t status = jtag->open_jlink_by_serial_wrapper(jlink_id);
        if (status != 0) {
            continue;
        }
        uint32_t id = jtag->read_id();
        if (id != WORMHOLE_ID) {
            log_warning(tt::LogSiliconDriver, "Only supporting WORMHOLE for now");
            jtag->close_jlink();
            continue;
        }

        jlink_devices.push_back(jlink_id);
        uint32_t efuse = jtag->read_axi(WORMHOLE_ARC_EFUSE_HARVESTING);

        uint32_t bad_mem_bits = efuse & 0x3FF;
        uint32_t bad_logic_bits = (efuse >> 10) & 0x3FF;

        /* each set bit inidicates a bad row */
        uint32_t bad_row_bits = (bad_mem_bits | bad_logic_bits);

        /* efuse_harvesting is the raw harvesting value from the efuse, it is
         * used for creating device soc descriptor, as UMD expects value in this
         * format */
        efuse_harvesting.push_back(bad_row_bits);

        jtag->close_jlink();
    }
    if (jlink_devices.empty()) {
        TT_THROW("There are no supported devices");
    }
}

/* static */ std::shared_ptr<JtagDevice> JtagDevice::create(std::filesystem::path& binary_directory) {
    if (binary_directory.empty()) {
        char buffer[PATH_MAX + 1];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

        if (len != -1) {
            buffer[len] = '\0';
            std::string path(buffer);
            std::string::size_type pos = path.find_last_of("/");
            binary_directory = path.substr(0, pos);
        }
    }

    std::unique_ptr<Jtag> jtag = std::make_unique<Jtag>(binary_directory.c_str());
    std::shared_ptr<JtagDevice> jtag_device = std::make_shared<JtagDevice>(std::move(jtag));

    // Check that all chips are of the same type
    auto arch = jtag_device->get_jtag_arch(0);
    for (size_t i = 1; i < jtag_device->get_device_cnt(); i++) {
        auto new_arch = jtag_device->get_jtag_arch(i);

        if (arch != new_arch) {
            TT_THROW("Jtag ERROR: Not all devices have the same architecture.");
        }
    }

    return jtag_device;
}

JtagDevice::~JtagDevice() {
    if (curr_device_idx.has_value()) {
        jtag->close_jlink();
        curr_device_idx = std::nullopt;
    }
}

uint32_t JtagDevice::get_device_cnt() const { return jlink_devices.size(); }

std::optional<uint32_t> JtagDevice::get_efuse_harvesting(uint8_t chip_id) const {
    if (chip_id >= get_device_cnt()) {
        return std::nullopt;
    }

    return efuse_harvesting[chip_id];
}

void JtagDevice::select_device(uint8_t chip_id) {
    if (chip_id >= get_device_cnt()) {
        TT_THROW(
            "JtagDevice::get_device_id: Device with chip_id {} doesn't exist. "
            "There are currently {} registered devices.",
            chip_id,
            get_device_cnt());
    }

    if (!curr_device_idx.has_value() || *curr_device_idx != chip_id) {
        jtag->close_jlink();

        // Underlying JTAG library uses unix-style status returns. Success is represented by 0.
        if (jtag->open_jlink_by_serial_wrapper(jlink_devices[chip_id])) {
            TT_THROW("JtagDevice::select_device: Failed to open JTAG device with chip_id {}", chip_id);
        }
        curr_device_idx = chip_id;
    }
}

tt::ARCH JtagDevice::get_jtag_arch(uint8_t chip_id) {
    auto arch_id = read_id(chip_id);

    if (!arch_id) {
        log_warning(tt::LogSiliconDriver, "Failed to read JTAG architecture for chip_id {}", chip_id);
        return tt::ARCH::Invalid;
    }

    uint32_t id = *arch_id;
    switch (id) {
        case WORMHOLE_ID:
            return tt::ARCH::WORMHOLE_B0;
        default:
            return tt::ARCH::Invalid;
    }
}

int JtagDevice::open_jlink_by_serial_wrapper(uint8_t chip_id, unsigned int serial_number) {
    jtag->close_jlink();
    return jtag->open_jlink_by_serial_wrapper(serial_number);
}

int JtagDevice::open_jlink_wrapper(uint8_t chip_id) { return jtag->open_jlink_wrapper(); }

std::optional<uint32_t> JtagDevice::read_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset) {
    select_device(chip_id);
    return jtag->read_tdr(client, reg_offset);
}

std::optional<uint32_t> JtagDevice::readmon_tdr(uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset) {
    select_device(chip_id);
    return jtag->readmon_tdr(client, id, reg_offset);
}

std::optional<int> JtagDevice::writemon_tdr(
    uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset, uint32_t data) {
    select_device(chip_id);
    jtag->writemon_tdr(client, id, reg_offset, data);
    return 0;
}

std::optional<int> JtagDevice::write_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset, uint32_t data) {
    select_device(chip_id);
    jtag->write_tdr(client, reg_offset, data);
    return 0;
}

void JtagDevice::dbus_memdump(
    uint8_t chip_id,
    const char* client_name,
    const char* mem,
    const char* thread_id_name,
    const char* start_addr,
    const char* end_addr) {
    select_device(chip_id);
    jtag->dbus_memdump(client_name, mem, thread_id_name, start_addr, end_addr);
}

std::optional<int> JtagDevice::dbus_sigdump(
    uint8_t chip_id,
    const char* client_name,
    uint32_t dbg_client_id,
    uint32_t dbg_signal_sel_start,
    uint32_t dbg_signal_sel_end) {
    select_device(chip_id);
    jtag->dbus_sigdump(client_name, dbg_client_id, dbg_signal_sel_start, dbg_signal_sel_end);
    return 0;
}

void JtagDevice::write32_axi(uint8_t chip_id, uint32_t address, uint32_t data) {
    select_device(chip_id);
    jtag->write_axi(address, data);
}

void JtagDevice::write32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) {
    select_device(chip_id);
    jtag->write_noc_xy(noc_x, noc_y, address, data);
}

void JtagDevice::write(
    uint8_t chip_id, const void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size) {
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(mem_ptr);

    const uint32_t chunk_size = sizeof(uint32_t);

    while (size > 0) {
        uint32_t transfer_size = std::min(size, chunk_size);

        // JTAG protocol doesn't require address alignment to word size (4 bytes).
        if (transfer_size == sizeof(uint32_t)) {
            uint32_t data;
            std::memcpy(&data, buffer_addr, transfer_size);
            write32(chip_id, noc_x, noc_y, addr, data);

            size -= transfer_size;
            addr += transfer_size;
            buffer_addr += transfer_size;
            continue;
        }

        auto read_result = read32(chip_id, noc_x, noc_y, addr);
        if (!read_result) {
            TT_THROW("JTAG read32 failed for device {} at core ({},{}) address 0x{:x}", chip_id, noc_x, noc_y, addr);
        }

        uint8_t* data_bytes = reinterpret_cast<uint8_t*>(&(*read_result));

        std::memcpy(data_bytes, buffer_addr, transfer_size);

        write32(chip_id, noc_x, noc_y, addr, *read_result);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

std::optional<uint32_t> JtagDevice::read32_axi(uint8_t chip_id, uint32_t address) {
    select_device(chip_id);
    return jtag->read_axi(address);
}

std::optional<uint32_t> JtagDevice::read32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) {
    select_device(chip_id);
    return jtag->read_noc_xy(noc_x, noc_y, address);
}

void JtagDevice::read(uint8_t chip_id, void* mem_ptr, uint8_t noc_x, uint8_t noc_y, uint64_t addr, uint32_t size) {
    uint8_t* buffer_addr = static_cast<uint8_t*>(mem_ptr);

    const uint32_t chunk_size = sizeof(uint32_t);

    while (size > 0) {
        uint32_t transfer_size = std::min(size, chunk_size);

        // JTAG protocol doesn't require address alignment to word size (4 bytes).
        auto result = read32(chip_id, noc_x, noc_y, addr);
        if (!result) {
            TT_THROW("JTAG read32 failed for device {} at core ({},{}) address 0x{:x}", chip_id, noc_x, noc_y, addr);
        }
        uint32_t data = *result;
        std::memcpy(buffer_addr, &data, transfer_size);

        size -= transfer_size;
        addr += transfer_size;
        buffer_addr += transfer_size;
    }
}

std::optional<std::vector<uint32_t>> JtagDevice::enumerate_jlink() { return jtag->enumerate_jlink(); }

void JtagDevice::close_jlink(uint8_t chip_id) {
    select_device(chip_id);
    jtag->close_jlink();
}

std::optional<uint32_t> JtagDevice::read_id_raw(uint8_t chip_id) {
    select_device(chip_id);
    return jtag->read_id_raw();
}

std::optional<uint32_t> JtagDevice::read_id(uint8_t chip_id) {
    select_device(chip_id);
    return jtag->read_id();
}

std::optional<uint8_t> JtagDevice::get_current_device_idx() const { return curr_device_idx; }

int JtagDevice::get_device_id(uint8_t chip_id) const {
    if (chip_id >= get_device_cnt()) {
        TT_THROW(
            "JtagDevice::get_device_id: Device with chip_id {} doesn't exist. "
            "There are currently {} registered devices.",
            chip_id,
            get_device_cnt());
    }
    return jlink_devices[chip_id];
}
