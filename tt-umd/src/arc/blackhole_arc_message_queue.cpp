// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/blackhole_arc_message_queue.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/unsupported.hpp"
#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/utils/lock_manager.hpp"

namespace tt::umd {

namespace {

// Spin briefly in case the firmware answers at once, which it usually does, then back off.
constexpr auto QUEUE_BUSY_POLL_WINDOW = std::chrono::microseconds(100);
constexpr auto QUEUE_POLL_INTERVAL = std::chrono::microseconds(10);

constexpr uint32_t STATUS_MASK = 0xFF;

}  // namespace

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
    // Other threads and processes post to the same queue; the ARC message lock lets one message through at a time.
    LockManager::initialize_mutex(MutexType::ARC_MSG, device_protocol->get_mmio_id(), IODeviceType::PCIe);
}

uint32_t BlackholeArcMessageQueue::send_message(
    const ArcMessageType message_type,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    if (args.size() > entry_len - 1) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("An ARC message takes at most {} arguments, {} were given.", entry_len - 1, args.size()));
    }

    // Word 0 carries the message code, the remaining words its arguments.
    std::array<uint32_t, entry_len> request{};
    request[0] = static_cast<uint32_t>(message_type);
    std::copy(args.begin(), args.end(), request.begin() + 1);

    std::array<uint32_t, entry_len> response{};
    {
        auto lock = LockManager::acquire_mutex(MutexType::ARC_MSG, device_protocol->get_mmio_id(), IODeviceType::PCIe);
        push_request(request, timeout_ms, noc_id);
        trigger_fw_int(noc_id);
        response = pop_response(timeout_ms, noc_id);
    }

    // Word 0 of the response is the status, statuses from ARC_MSG_RESPONSE_OK_LIMIT up mean the firmware did not
    // handle the message; the remaining words are its return values.
    const uint32_t status = response[0] & STATUS_MASK;
    if (status >= blackhole::ARC_MSG_RESPONSE_OK_LIMIT) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "ARC firmware failed to handle message {:#x}: status {:#x}.",
                static_cast<uint32_t>(message_type),
                status));
    }
    return_values.assign(response.begin() + 1, response.end());
    return status;
}

std::unique_ptr<BlackholeArcMessageQueue> BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
    DeviceProtocol* device_protocol,
    JtagInterface* jtag_interface,
    BlackholeArcApb* arc_apb,
    const bool noc_translation_enabled,
    const size_t queue_index,
    const NocId noc_id) {
    if (jtag_interface != nullptr) {
        UMD_NOT_SUPPORTED("ARC messages over JTAG");
    }
    const tt_xy_pair arc_core = blackhole::get_arc_core(noc_translation_enabled, noc_id == NocId::NOC1);

    uint32_t queue_control_block_addr = 0;
    arc_apb->read(&queue_control_block_addr, blackhole::SCRATCH_RAM_11, sizeof(uint32_t), arc_core, noc_id);

    uint32_t queue_control_block[2] = {};
    device_protocol->read_ctrl(
        queue_control_block,
        arc_core,
        queue_control_block_addr,
        sizeof(queue_control_block),
        noc_id,
        RegisterAccessWidth::BITS32);
    const uint32_t first_queue_addr = queue_control_block[0];
    const uint32_t entries_per_queue = queue_control_block[1] & 0xFF;
    const uint32_t num_queues = (queue_control_block[1] >> 8) & 0xFF;
    if (queue_index >= num_queues) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("ARC firmware has {} message queues, queue {} was asked for.", num_queues, queue_index));
    }

    const uint64_t queue_stride =
        blackhole::ARC_MSG_QUEUE_HEADER_SIZE + 2 * entries_per_queue * blackhole::ARC_QUEUE_ENTRY_SIZE;
    return std::make_unique<BlackholeArcMessageQueue>(
        device_protocol,
        arc_apb,
        first_queue_addr + queue_index * queue_stride,
        entries_per_queue,
        noc_translation_enabled);
}

void BlackholeArcMessageQueue::push_request(
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len>& request,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    uint32_t request_wptr = 0;
    const bool has_space = utils::poll_until(
        [&]() {
            request_wptr = read_word(request_wptr_offset, noc_id);
            const uint32_t request_rptr = read_word(request_rptr_offset, noc_id);
            return (request_wptr - request_rptr) % (2 * size) < size;
        },
        timeout_ms,
        QUEUE_BUSY_POLL_WINDOW,
        QUEUE_POLL_INTERVAL);
    if (!has_space) {
        UMD_THROW(error::RuntimeError, "ARC message request queue stayed full.");
    }

    write_words(request.data(), entry_len, header_len + (request_wptr % size) * entry_len, noc_id);
    uint32_t next_wptr = (request_wptr + 1) % (2 * size);
    write_words(&next_wptr, 1, request_wptr_offset, noc_id);
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::pop_response(
    const std::chrono::milliseconds timeout_ms, const NocId noc_id) {
    const uint32_t response_rptr = read_word(response_rptr_offset, noc_id);
    const bool answered = utils::poll_until(
        [&]() { return read_word(response_wptr_offset, noc_id) != response_rptr; },
        timeout_ms,
        QUEUE_BUSY_POLL_WINDOW,
        QUEUE_POLL_INTERVAL);
    if (!answered) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("ARC firmware did not answer a message within {} ms.", timeout_ms.count()));
    }

    std::array<uint32_t, entry_len> response{};
    read_words(response.data(), entry_len, header_len + (size + response_rptr % size) * entry_len, noc_id);
    uint32_t next_rptr = (response_rptr + 1) % (2 * size);
    write_words(&next_rptr, 1, response_rptr_offset, noc_id);
    return response;
}

void BlackholeArcMessageQueue::read_words(uint32_t* data, size_t num_words, size_t offset, const NocId noc_id) {
    device_protocol->read_ctrl(
        data,
        get_arc_core(noc_id),
        base_address + offset * sizeof(uint32_t),
        num_words * sizeof(uint32_t),
        noc_id,
        RegisterAccessWidth::BITS32);
}

uint32_t BlackholeArcMessageQueue::read_word(size_t offset, const NocId noc_id) {
    uint32_t word = 0;
    read_words(&word, 1, offset, noc_id);
    return word;
}

void BlackholeArcMessageQueue::write_words(uint32_t* data, size_t num_words, size_t offset, const NocId noc_id) {
    device_protocol->write_ctrl(
        data,
        get_arc_core(noc_id),
        base_address + offset * sizeof(uint32_t),
        num_words * sizeof(uint32_t),
        noc_id,
        RegisterAccessWidth::BITS32);
}

void BlackholeArcMessageQueue::trigger_fw_int(const NocId noc_id) {
    const tt_xy_pair arc_core = get_arc_core(noc_id);
    const auto trigger_clear = [&]() {
        uint32_t trigger = 0;
        arc_apb->read(&trigger, blackhole::ARC_FW_INT_ADDR, sizeof(trigger), arc_core, noc_id);
        return (trigger & blackhole::ARC_FW_INT_VAL) == 0;
    };
    // A trigger still pending from another process's message makes the firmware scan the queues anyway, so a
    // trigger that does not clear in time is not an error; ours is raised regardless.
    utils::poll_until(trigger_clear, timeout::ARC_TRIGGER_CLEAR_TIMEOUT, QUEUE_BUSY_POLL_WINDOW, QUEUE_POLL_INTERVAL);
    const uint32_t trigger = blackhole::ARC_FW_INT_VAL;
    arc_apb->write(&trigger, blackhole::ARC_FW_INT_ADDR, sizeof(trigger), arc_core, noc_id);
}

tt_xy_pair BlackholeArcMessageQueue::get_arc_core(const NocId noc_id) const {
    return blackhole::get_arc_core(noc_translation_enabled, noc_id == NocId::NOC1);
}

}  // namespace tt::umd
