// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/jtag_device.h"

#include <cstdint>
#include <iostream>

#include "device/lib/jtag/inc/jtag.h"
#include "umd/device/cluster.h"
#include "umd/device/tt_xy_pair.h"

#define ROW_LEN 12
#define WORMHOLE_ID 0x138a5
#define WORMHOLE_ARC_EFUSE_BOX1 0x80042000
#define WORMHOLE_ARC_EFUSE_HARVESTING (WORMHOLE_ARC_EFUSE_BOX1 + 0x25C)

JtagDevice::JtagDevice(std::unique_ptr<Jtag> jtag_device) : jtag(std::move(jtag_device)) {
    jtag->close_jlink();

    std::vector<uint32_t> potential_devices = jtag->enumerate_jlink();
    if (potential_devices.empty()) {
        throw std::runtime_error("There are no devices");
    }

    for (int jlink_id : potential_devices) {
        uint32_t status = jtag->open_jlink_by_serial_wrapper(jlink_id);
        if (status != 0) {
            continue;
        }
        uint32_t id = jtag->read_id();
        if (id != WORMHOLE_ID) {
            std::cerr << "Only supporting WORMHOLE for now" << std::endl;
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
        throw std::runtime_error("There are no supported devices");
    }

    curr_device_idx = 0xff;
}

JtagDevice::~JtagDevice() {
    if (curr_device_idx != 0xff) {
        jtag->close_jlink();
        curr_device_idx = 0xff;
    }
}

std::optional<uint32_t> JtagDevice::get_device_cnt() const { return jlink_devices.size(); }

std::optional<uint32_t> JtagDevice::get_efuse_harvesting(uint8_t chip_id) const {
    if (chip_id >= get_device_cnt()) {
        return {};
    }

    return efuse_harvesting[chip_id];
}

bool JtagDevice::select_device(uint8_t chip_id) {
    if (chip_id >= get_device_cnt()) {
        return false;
    }

    if (curr_device_idx != chip_id) {
        curr_device_idx = chip_id;
        jtag->close_jlink();
        jtag->open_jlink_by_serial_wrapper(jlink_devices[curr_device_idx]);
    }
    return true;
}

std::optional<tt::ARCH> JtagDevice::get_jtag_arch(uint8_t chip_id) {
    auto id_opt = read_id(chip_id);

    if (!id_opt) {
        return {};
    }

    uint32_t id = *id_opt;
    switch (id) {
        case WORMHOLE_ID:
            return tt::ARCH::WORMHOLE_B0;
        default:
            return tt::ARCH::Invalid;
    }
}

std::optional<int> JtagDevice::open_jlink_by_serial_wrapper(uint8_t chip_id, unsigned int serial_number) {
    jtag->close_jlink();
    return jtag->open_jlink_by_serial_wrapper(serial_number);
}

std::optional<int> JtagDevice::open_jlink_wrapper(uint8_t chip_id) { return jtag->open_jlink_wrapper(); }

std::optional<uint32_t> JtagDevice::read_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset) {
    if (!select_device(chip_id)) {
        return {};
    }
    return jtag->read_tdr(client, reg_offset);
}

std::optional<uint32_t> JtagDevice::readmon_tdr(uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset) {
    if (!select_device(chip_id)) {
        return {};
    }
    return jtag->readmon_tdr(client, id, reg_offset);
}

std::optional<int> JtagDevice::writemon_tdr(uint8_t chip_id, const char* client, uint32_t id, uint32_t reg_offset,
                                            uint32_t data) {
    if (!select_device(chip_id)) {
        return {};
    }
    jtag->writemon_tdr(client, id, reg_offset, data);
    return 0;
}

std::optional<int> JtagDevice::write_tdr(uint8_t chip_id, const char* client, uint32_t reg_offset, uint32_t data) {
    if (!select_device(chip_id)) {
        return {};
    }
    jtag->write_tdr(client, reg_offset, data);
    return 0;
}

std::optional<int> JtagDevice::dbus_memdump(uint8_t chip_id, const char* client_name, const char* mem,
                                            const char* thread_id_name, const char* start_addr, const char* end_addr) {
    if (!select_device(chip_id)) {
        return {};
    }
    jtag->dbus_memdump(client_name, mem, thread_id_name, start_addr, end_addr);
    return 0;
}

std::optional<int> JtagDevice::dbus_sigdump(uint8_t chip_id, const char* client_name, uint32_t dbg_client_id,
                                            uint32_t dbg_signal_sel_start, uint32_t dbg_signal_sel_end) {
    if (!select_device(chip_id)) {
        return {};
    }
    jtag->dbus_sigdump(client_name, dbg_client_id, dbg_signal_sel_start, dbg_signal_sel_end);
    return 0;
}

std::optional<int> JtagDevice::write32_axi(uint8_t chip_id, uint32_t address, uint32_t data) {
    if (!select_device(chip_id)) {
        return {};
    }

    jtag->write_axi(address, data);
    return 4;
}

std::optional<int> JtagDevice::write32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address, uint32_t data) {
    if (!select_device(chip_id)) {
        return {};
    }

    jtag->write_noc_xy(noc_x, noc_y, address, data);
    return 4;
}

std::optional<uint32_t> JtagDevice::read32_axi(uint8_t chip_id, uint32_t address) {
    if (!select_device(chip_id)) {
        return {};
    }

    return jtag->read_axi(address);
}

std::optional<uint32_t> JtagDevice::read32(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address) {
    if (!select_device(chip_id)) {
        return {};
    }

    return jtag->read_noc_xy(noc_x, noc_y, address);
}

std::optional<std::vector<uint32_t>> JtagDevice::enumerate_jlink(uint8_t chip_id) { return jtag->enumerate_jlink(); }

std::optional<int> JtagDevice::close_jlink(uint8_t chip_id) {
    if (!select_device(chip_id)) {
        return {};
    }

    jtag->close_jlink();
    return 0;
}

std::optional<uint32_t> JtagDevice::read_id_raw(uint8_t chip_id) {
    if (!select_device(chip_id)) {
        return {};
    }

    return jtag->read_id_raw();
}

std::optional<uint32_t> JtagDevice::read_id(uint8_t chip_id) {
    if (!select_device(chip_id)) {
        return {};
    }

    return jtag->read_id();
}
