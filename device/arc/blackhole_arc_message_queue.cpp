// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_message_queue.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

#include "noc_access.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "utils.hpp"

namespace tt::umd {

BlackholeArcMessageQueue::BlackholeArcMessageQueue(
    TTDevice* tt_device, const uint64_t base_address, const uint64_t size, const tt_xy_pair arc_core) :
    base_address(base_address), size(size), tt_device(tt_device), arc_core(arc_core) {}

void BlackholeArcMessageQueue::read_words(uint32_t* data, size_t num_words, size_t offset) {
    tt_device->read_from_device(data, arc_core, base_address + offset * sizeof(uint32_t), num_words * sizeof(uint32_t));
}

uint32_t BlackholeArcMessageQueue::read_word(size_t offset) {
    uint32_t word;
    read_words(&word, 1, offset);
    return word;
}

void BlackholeArcMessageQueue::write_words(uint32_t* data, size_t num_words, size_t offset) {
    tt_device->write_to_device(data, arc_core, base_address + offset * sizeof(uint32_t), num_words * sizeof(uint32_t));
}

void BlackholeArcMessageQueue::trigger_fw_int() {
    tt_device->write_to_arc_apb(&ARC_FW_INT_VAL, ARC_FW_INT_ADDR, sizeof(uint32_t));
}

void BlackholeArcMessageQueue::push_request(
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len>& request, const std::chrono::milliseconds timeout_ms) {
    uint32_t request_queue_wptr = read_word(request_wptr_offset);

    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t const request_queue_rptr = read_word(request_rptr_offset);
        if (abs((int)request_queue_rptr - (int)request_queue_wptr) % (2 * size) != size) {
            break;
        }

        utils::check_timeout(start, timeout_ms, "Timeout waiting for ARC msg request queue.");
    }

    // Offset in words.
    uint32_t const request_entry_offset = header_len + (request_queue_wptr % size) * BlackholeArcMessageQueue::entry_len;
    write_words(request.data(), BlackholeArcMessageQueue::entry_len, request_entry_offset);

    request_queue_wptr = (request_queue_wptr + 1) % (2 * size);
    write_words(&request_queue_wptr, 1, request_wptr_offset);

    trigger_fw_int();
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::pop_response(
    const std::chrono::milliseconds timeout_ms) {
    uint32_t response_queue_rptr = read_word(response_rptr_offset);

    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t const response_queue_wptr = read_word(response_wptr_offset);

        if (response_queue_rptr != response_queue_wptr) {
            break;
        }

        utils::check_timeout(start, timeout_ms, "Timeout waiting for ARC msg request queue.");
    }

    uint32_t const response_entry_offset =
        header_len + (size + (response_queue_rptr % size)) * BlackholeArcMessageQueue::entry_len;
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> response;
    read_words(response.data(), BlackholeArcMessageQueue::entry_len, response_entry_offset);

    response_queue_rptr = (response_queue_rptr + 1) % (2 * size);
    write_words(&response_queue_rptr, 1, response_rptr_offset);

    return response;
}

uint32_t BlackholeArcMessageQueue::send_message(
    const ArcMessageType message_type, const std::vector<uint32_t>& args, const std::chrono::milliseconds timeout_ms) {
    if (args.size() > 7) {
        throw std::runtime_error(
            fmt::format("Blackhole ARC messages are limited to 7 arguments, but: {} were provided", args.size()));
    }

    // Initialize with zeros for unused args.
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> request = {(uint32_t)message_type, 0, 0, 0, 0, 0, 0, 0};

    // Copy provided arguments.
    for (size_t i = 0; i < args.size(); i++) {
        request[i + 1] = args[i];
    }

    push_request(request, timeout_ms);

    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> response = pop_response(timeout_ms);

    uint32_t status = response[0] & 0xFF;

    // Response is packed in high 16 bits of the message.
    if (status < blackhole::ARC_MSG_RESPONSE_OK_LIMIT) {
        return response[0] >> 16;
    } else if (status == 0xFF) {
        throw std::runtime_error(fmt::format("Message code {} not recognized by ARC fw.", (uint32_t)message_type));
        return 0;
    } else {
        throw std::runtime_error(fmt::format("Uknown message error code {}", status));
        return 0;
    }
}

std::unique_ptr<BlackholeArcMessageQueue> BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
    TTDevice* tt_device, const size_t queue_index) {
    const tt_xy_pair arc_core = blackhole::get_arc_core(tt_device->get_noc_translation_enabled(), is_selected_noc1());

    uint32_t queue_control_block_addr;
    tt_device->read_from_arc_apb(&queue_control_block_addr, blackhole::SCRATCH_RAM_11, sizeof(uint32_t));

    uint64_t queue_control_block;
    if (tt_device->get_communication_device_type() == IODeviceType::JTAG) {
        queue_control_block = tt_device->get_jtag_device()->read32_axi(0, queue_control_block_addr).value();
        queue_control_block |=
            ((uint64_t)tt_device->get_jtag_device()->read32_axi(0, queue_control_block_addr + 4).value() << 32);
    } else {
        tt_device->read_from_device(&queue_control_block, arc_core, queue_control_block_addr, sizeof(uint64_t));
    }

    uint32_t const queue_base_addr = queue_control_block & 0xFFFFFFFF;
    uint32_t const num_entries_per_queue = (queue_control_block >> 32) & 0xFF;

    uint32_t const msg_queue_size = 2 * num_entries_per_queue * ARC_QUEUE_ENTRY_SIZE + ARC_MSG_QUEUE_HEADER_SIZE;
    uint32_t const msg_queue_base = queue_base_addr + queue_index * msg_queue_size;

    return std::make_unique<BlackholeArcMessageQueue>(tt_device, msg_queue_base, num_entries_per_queue, arc_core);
}

}  // namespace tt::umd
