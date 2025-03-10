// SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/wormhole_tt_device.h"

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

    chip_info.noc_translation_enabled = noc_translation_enabled;

    std::vector<uint32_t> arc_msg_return_values = {0};
    const uint32_t timeout_ms = 1000;
    uint32_t ret_code = get_arc_messenger()->send_message(
        tt::umd::wormhole::ARC_MSG_COMMON_PREFIX |
            get_architecture_implementation()->get_arc_message_arc_get_harvesting(),
        arc_msg_return_values,
        0,
        0,
        timeout_ms);

    if (ret_code != 0) {
        throw std::runtime_error(fmt::format("Failed to get harvesting masks with exit code {}", ret_code));
    }

    chip_info.harvesting_masks.tensix_harvesting_mask = arc_msg_return_values[0];

    chip_info.board_type = get_board_type();

    return chip_info;
}

void WormholeTTDevice::wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms) {}

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

    tt_xy_pair arc_core = tt::umd::wormhole::ARC_CORES[0];
    static constexpr uint64_t noc_telemetry_offset = 0x810000000;
    uint64_t telemetry_struct_offset = arc_msg_return_values[0] + noc_telemetry_offset;

    uint32_t board_id_lo;
    uint32_t board_id_hi;
    static uint64_t board_id_hi_telemetry_offset = 16;
    static uint64_t board_id_lo_telemetry_offset = 20;
    read_from_device(&board_id_hi, arc_core, telemetry_struct_offset + board_id_hi_telemetry_offset, sizeof(uint32_t));
    read_from_device(&board_id_lo, arc_core, telemetry_struct_offset + board_id_lo_telemetry_offset, sizeof(uint32_t));

    return get_board_type_from_board_id(((uint64_t)board_id_hi << 32) | board_id_lo);
}

}  // namespace tt::umd
