// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/arc/blackhole_arc_message_queue.hpp"

#include <fmt/format.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/utils/error.hpp"
#include "utils.hpp"

namespace tt::umd::blackhole {
enum class ArcMessageType : uint8_t;
}  // namespace tt::umd::blackhole

namespace tt::umd {

BlackholeArcMessageQueue::BlackholeArcMessageQueue(
    DeviceProtocol* device_protocol,
    BlackholeArcApb* arc_apb,
    const uint64_t base_address,
    const uint64_t size,
    const bool noc_translation_enabled) :
    base_address(base_address),
    size(size),
    device_protocol(device_protocol),
    arc_apb(arc_apb),
    noc_translation_enabled(noc_translation_enabled) {
    UMD_ASSERT(device_protocol != nullptr, error::RuntimeError, "BlackholeArcMessageQueue requires a DeviceProtocol.");
    UMD_ASSERT(arc_apb != nullptr, error::RuntimeError, "BlackholeArcMessageQueue requires a BlackholeArcApb.");
}

tt_xy_pair BlackholeArcMessageQueue::get_arc_core(const NocId noc_id) const {
    return blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/noc_id == NocId::NOC1);
}

void BlackholeArcMessageQueue::read_words(uint32_t* data, size_t num_words, size_t offset, const NocId noc_id) {
    device_protocol->read_data(
        data, get_arc_core(noc_id), base_address + offset * sizeof(uint32_t), num_words * sizeof(uint32_t), noc_id);
}

uint32_t BlackholeArcMessageQueue::read_word(size_t offset, const NocId noc_id) {
    uint32_t word;
    read_words(&word, 1, offset, noc_id);
    return word;
}

void BlackholeArcMessageQueue::write_words(uint32_t* data, size_t num_words, size_t offset, const NocId noc_id) {
    device_protocol->write_data(
        data, get_arc_core(noc_id), base_address + offset * sizeof(uint32_t), num_words * sizeof(uint32_t), noc_id);
}

void BlackholeArcMessageQueue::trigger_fw_int(const NocId noc_id) {
    arc_apb->write(&ARC_FW_INT_VAL, ARC_FW_INT_ADDR, sizeof(uint32_t), get_arc_core(noc_id), noc_id);
}

void BlackholeArcMessageQueue::push_request(
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len>& request,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    uint32_t request_queue_wptr = read_word(request_wptr_offset, noc_id);

    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t request_queue_rptr = read_word(request_rptr_offset, noc_id);
        if (abs((int)request_queue_rptr - (int)request_queue_wptr) % (2 * size) != size) {
            break;
        }

        utils::check_timeout(start, timeout_ms, "Timeout waiting for ARC msg request queue.");
    }

    // Offset in words.
    uint32_t request_entry_offset = header_len + (request_queue_wptr % size) * BlackholeArcMessageQueue::entry_len;
    write_words(request.data(), BlackholeArcMessageQueue::entry_len, request_entry_offset, noc_id);

    request_queue_wptr = (request_queue_wptr + 1) % (2 * size);
    write_words(&request_queue_wptr, 1, request_wptr_offset, noc_id);

    trigger_fw_int(noc_id);
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::pop_response(
    const std::chrono::milliseconds timeout_ms, const NocId noc_id) {
    uint32_t response_queue_rptr = read_word(response_rptr_offset, noc_id);

    auto start = std::chrono::steady_clock::now();
    while (true) {
        uint32_t response_queue_wptr = read_word(response_wptr_offset, noc_id);

        if (response_queue_rptr != response_queue_wptr) {
            break;
        }

        utils::check_timeout(start, timeout_ms, "Timeout waiting for ARC msg request queue.");
    }

    uint32_t response_entry_offset =
        header_len + (size + (response_queue_rptr % size)) * BlackholeArcMessageQueue::entry_len;
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> response;
    read_words(response.data(), BlackholeArcMessageQueue::entry_len, response_entry_offset, noc_id);

    response_queue_rptr = (response_queue_rptr + 1) % (2 * size);
    write_words(&response_queue_rptr, 1, response_rptr_offset, noc_id);

    return response;
}

uint32_t BlackholeArcMessageQueue::send_message(
    const ArcMessageType message_type,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    if (args.size() > 7) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Blackhole ARC messages are limited to 7 arguments, but: {} were provided.", args.size()));
    }

    // Initialize with zeros for unused args.
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> request = {(uint32_t)message_type, 0, 0, 0, 0, 0, 0, 0};

    // Copy provided arguments.
    for (size_t i = 0; i < args.size(); i++) {
        request[i + 1] = args[i];
    }

    push_request(request, timeout_ms, noc_id);

    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> response = pop_response(timeout_ms, noc_id);

    uint32_t status = response[0] & 0xFF;

    return_values.assign(response.begin() + 1, response.end());

    // Response is packed in high 16 bits of the message.
    if (status < blackhole::ARC_MSG_RESPONSE_OK_LIMIT) {
        return response[0] >> 16;
    } else if (status == 0xFF) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Message code: {} not recognized by ARC firmware.", (uint32_t)message_type));
        return 0;
    } else {
        UMD_THROW(error::RuntimeError, fmt::format("Unknown message error code: {}", status));
        return 0;
    }
}

/* static */ std::unique_ptr<BlackholeArcMessageQueue> BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
    DeviceProtocol* device_protocol,
    JtagInterface* jtag_interface,
    BlackholeArcApb* arc_apb,
    const bool noc_translation_enabled,
    const size_t queue_index,
    const NocId noc_id) {
    // Checked here as well as in the constructor: both are dereferenced below to read the queue
    // descriptor, which happens before there is an object to construct. jtag_interface is exempt --
    // it is optional, and its being null is what selects the non-JTAG route.
    UMD_ASSERT(device_protocol != nullptr, error::RuntimeError, "BlackholeArcMessageQueue requires a DeviceProtocol.");
    UMD_ASSERT(arc_apb != nullptr, error::RuntimeError, "BlackholeArcMessageQueue requires a BlackholeArcApb.");

    const tt_xy_pair arc_core = blackhole::get_arc_core(noc_translation_enabled, /*use_noc1=*/noc_id == NocId::NOC1);

    uint32_t queue_control_block_addr;
    arc_apb->read(&queue_control_block_addr, blackhole::SCRATCH_RAM_11, sizeof(uint32_t), arc_core, noc_id);

    uint64_t queue_control_block;
    if (jtag_interface != nullptr) {
        queue_control_block = jtag_interface->mmio_read32(queue_control_block_addr);
        queue_control_block |= ((uint64_t)jtag_interface->mmio_read32(queue_control_block_addr + 4) << 32);
    } else {
        device_protocol->read_data(&queue_control_block, arc_core, queue_control_block_addr, sizeof(uint64_t), noc_id);
    }

    uint32_t queue_base_addr = queue_control_block & 0xFFFFFFFF;
    uint32_t num_entries_per_queue = (queue_control_block >> 32) & 0xFF;

    uint32_t msg_queue_size = 2 * num_entries_per_queue * ARC_QUEUE_ENTRY_SIZE + ARC_MSG_QUEUE_HEADER_SIZE;
    uint32_t msg_queue_base = queue_base_addr + queue_index * msg_queue_size;

    return std::make_unique<BlackholeArcMessageQueue>(
        device_protocol, arc_apb, msg_queue_base, num_entries_per_queue, noc_translation_enabled);
}

}  // namespace tt::umd
