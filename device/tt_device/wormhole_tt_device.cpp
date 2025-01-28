// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/wormhole_tt_device.h"

#include <iostream>

#include "logger.hpp"
#include "umd/device/wormhole_implementation.h"

namespace tt::umd {

WormholeTTDevice::WormholeTTDevice(std::unique_ptr<PCIDevice> pci_device) :
    TTDevice(std::move(pci_device), std::make_unique<wormhole_implementation>()) {}

ChipInfo WormholeTTDevice::get_chip_info() {
    ChipInfo chip_info;

    uint32_t niu_cfg;
    const tt_xy_pair dram_core = {0, 0};
    const uint64_t niu_cfg_addr = 0x1000A0000 + 0x100;
    read_from_device(&niu_cfg, dram_core, niu_cfg_addr, sizeof(uint32_t));

    bool noc_translation_enabled = (niu_cfg & (1 << 14)) != 0;

    std::cout << "noc translation enabled " << noc_translation_enabled << std::endl;

    chip_info.noc_translation_enabled = noc_translation_enabled;

    uint32_t harv;
    arc_msg(0xaa00 | get_architecture_implementation()->get_arc_message_arc_get_harvesting(), true, 0, 0, 1, &harv);

    std::cout << "harv " << harv << std::endl;

    chip_info.harvesting_masks.tensix_harvesting_mask = harv;

    // uint32_t telemetry_addr;
    // arc_msg(0xaa2c, true, 0, 0, 1, &telemetry_addr);

    // std::cout << std::hex;
    // std::cout << "telemetry addr " << telemetry_addr << std::endl;
    // std::cout << std::dec;

    // uint32_t eth_fw_version;
    // read_from_device(&eth_fw_version, {0, 10}, telemetry_addr + 44, sizeof(uint32_t));

    // std::cout << std::hex;
    // std::cout << "eth fw version " << eth_fw_version << std::endl;
    // std::cout << std::dec;

    return chip_info;
}

void WormholeTTDevice::wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms) {}

void WormholeTTDevice::bar_write32(uint32_t addr, uint32_t data) {
    if (addr < get_pci_device()->bar0_uc_offset) {
        write_block(addr, sizeof(data), reinterpret_cast<const uint8_t*>(&data));  // do we have to reinterpret_cast?
    } else {
        write_regs(addr, 1, &data);
    }
}

uint32_t WormholeTTDevice::bar_read32(uint32_t addr) {
    uint32_t data;
    if (addr < get_pci_device()->bar0_uc_offset) {
        read_block(addr, sizeof(data), reinterpret_cast<uint8_t*>(&data));
    } else {
        read_regs(addr, 1, &data);
    }
    return data;
}

int WormholeTTDevice::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    int timeout,
    uint32_t* return_3,
    uint32_t* return_4) {
    static const uint32_t MSG_ERROR_REPLY = 0xFFFFFFFF;
    if ((msg_code & 0xff00) != 0xaa00) {
        log_error("Malformed message. msg_code is 0x{:x} but should be 0xaa..", msg_code);
    }
    log_assert(arg0 <= 0xffff and arg1 <= 0xffff, "Only 16 bits allowed in arc_msg args");  // Only 16 bits are
    // allowed

    auto architecture_implementation = get_architecture_implementation();

    // // Exclusive access for a single process at a time. Based on physical pci interface id.
    // std::string msg_type = "ARC_MSG";
    // const scoped_lock<named_mutex> lock(*get_mutex(msg_type, logical_device_id));
    uint32_t fw_arg = arg0 | (arg1 << 16);
    int exit_code = 0;

    bar_write32(architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4, fw_arg);
    bar_write32(architecture_implementation->get_arc_reset_scratch_offset() + 5 * 4, msg_code);

    uint32_t misc = bar_read32(architecture_implementation->get_arc_reset_arc_misc_cntl_offset());
    if (misc & (1 << 16)) {
        log_error("trigger_fw_int failed on device {}", 0);
        return 1;
    } else {
        bar_write32(architecture_implementation->get_arc_reset_arc_misc_cntl_offset(), misc | (1 << 16));
    }

    if (wait_for_done) {
        uint32_t status = 0xbadbad;
        auto timeout_seconds = std::chrono::seconds(timeout);
        auto start = std::chrono::system_clock::now();
        while (true) {
            if (std::chrono::system_clock::now() - start > timeout_seconds) {
                throw std::runtime_error(
                    fmt::format("Timed out after waiting {} seconds for device {} ARC to respond", timeout, 0));
            }

            status = bar_read32(architecture_implementation->get_arc_reset_scratch_offset() + 5 * 4);

            if ((status & 0xffff) == (msg_code & 0xff)) {
                if (return_3 != nullptr) {
                    *return_3 = bar_read32(architecture_implementation->get_arc_reset_scratch_offset() + 3 * 4);
                }

                if (return_4 != nullptr) {
                    *return_4 = bar_read32(architecture_implementation->get_arc_reset_scratch_offset() + 4 * 4);
                }

                exit_code = (status & 0xffff0000) >> 16;
                break;
            } else if (status == MSG_ERROR_REPLY) {
                log_warning(LogSiliconDriver, "On device {}, message code 0x{:x} not recognized by FW", 0, msg_code);
                exit_code = MSG_ERROR_REPLY;
                break;
            }
        }
    }

    detect_hang_read();
    return exit_code;
}

uint32_t WormholeTTDevice::get_clock() {
    const uint32_t timeouts_ms = 1000;
    // There is one return value from AICLK ARC message.
    std::vector<uint32_t> arc_msg_return_values = {0};
    auto exit_code = get_arc_messenger()->send_message(
        tt::umd::wormhole::ARC_MSG_COMMON_PREFIX | get_architecture_implementation()->get_arc_message_get_aiclk(),
        arc_msg_return_values,
        0xFFFF,
        0xFFFF,
        timeouts_ms);
    if (exit_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get AICLK value with exit code {}", exit_code));
    }
    return arc_msg_return_values[0];
}

BoardType WormholeTTDevice::get_board_type() {
    ::std::vector<uint32_t> arc_msg_return_values = {0};
    static const uint32_t timeout_ms = 1000;
    uint32_t exit_code = get_arc_messenger()->send_message(
        tt::umd::wormhole::ARC_MSG_COMMON_PREFIX |
            (uint32_t)tt::umd::wormhole::arc_message_type::GET_SMBUS_TELEMETRY_ADDR,
        arc_msg_return_values,
        0,
        0,
        timeout_ms);

    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    uint64_t telemetry_struct_offset = arc_msg_return_values[0] + noc_telemetry_offset;

    uint32_t board_id_lo;
    uint32_t board_id_hi;
    tt_xy_pair arc_core = tt::umd::wormhole::ARC_CORES[0];
    static uint64_t board_id_hi_telemetry_offset = 16;
    static uint64_t board_id_lo_telemetry_offset = 20;
    read_from_device(&board_id_hi, arc_core, telemetry_struct_offset + board_id_hi_telemetry_offset, sizeof(uint32_t));
    read_from_device(&board_id_lo, arc_core, telemetry_struct_offset + board_id_lo_telemetry_offset, sizeof(uint32_t));

    return get_board_type_from_board_id(((uint64_t)board_id_hi << 32) | board_id_lo);
}

}  // namespace tt::umd
