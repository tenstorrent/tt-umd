// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/arc/blackhole_arc_message_queue.hpp"

#include <fmt/format.h>

#include <chrono>
#include <thread>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd/arch/architecture_registers.hpp"
#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt-umd/utils/lock_manager.hpp"

namespace tt::umd {

namespace {

// The queues live in ARC CSM; a queue info pointer outside it is not one the firmware published.
constexpr uint64_t ARC_CSM_BASE = 0x10000000;
constexpr uint64_t ARC_CSM_SIZE = 512 * 1024;

// Writing 0 to the ARC's MSI catcher FIFO is the doorbell that makes the firmware run its queues.
constexpr uint32_t ARC_MSI_CATCHER_OFFSET = 0xB0000;

constexpr uint32_t QUEUE_ENTRY_COUNT_MASK = 0xFF;
constexpr uint32_t QUEUE_COUNT_SHIFT = 8;
constexpr uint32_t QUEUE_COUNT_MASK = 0xFF;
constexpr uint32_t STATUS_MASK = 0xFF;

// Pointers poll in a tight loop at first, since the firmware answers most messages within
// microseconds, then back off so a slow handler does not keep the NOC busy.
constexpr auto BUSY_POLL_WINDOW = std::chrono::microseconds(200);
constexpr auto POLL_INTERVAL = std::chrono::microseconds(20);

// Waits until ready() holds, polling it. Returns false on timeout.
template <typename Predicate>
bool poll_until(Predicate ready, std::chrono::milliseconds timeout) {
    const auto start = std::chrono::steady_clock::now();
    while (!ready()) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > timeout) {
            return false;
        }
        if (elapsed > BUSY_POLL_WINDOW) {
            std::this_thread::sleep_for(POLL_INTERVAL);
        }
    }
    return true;
}

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
    LockManager::initialize_mutex(MutexType::ARC_MSG, device_protocol->get_mmio_id(), IODeviceType::PCIe);
}

uint32_t BlackholeArcMessageQueue::send_message(
    const ArcMessageType message_type,
    std::vector<uint32_t>& return_values,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    UMD_ASSERT(
        args.size() < entry_len,
        error::RuntimeError,
        fmt::format("An ARC message takes at most {} arguments, got {}.", entry_len - 1, args.size()));

    std::array<uint32_t, entry_len> request = {static_cast<uint32_t>(message_type)};
    std::copy(args.begin(), args.end(), request.begin() + 1);

    // The queue pointers are read-modify-written by the host, so only one writer may drive the queue at a time: the
    // lock serializes threads and every process sharing the device.
    auto lock = LockManager::acquire_mutex(MutexType::ARC_MSG, device_protocol->get_mmio_id(), IODeviceType::PCIe);

    push_request(request, timeout_ms, noc_id);
    const std::array<uint32_t, entry_len> response = pop_response(timeout_ms, noc_id);

    return_values.assign(response.begin() + 1, response.end());
    const uint32_t status = response[0] & STATUS_MASK;
    log_trace(LogUMD, "ARC message {:#x} returned status {:#x}.", static_cast<uint32_t>(message_type), status);
    return status;
}

std::unique_ptr<BlackholeArcMessageQueue> BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
    DeviceProtocol* device_protocol,
    JtagInterface* /*jtag_interface*/,
    BlackholeArcApb* arc_apb,
    const bool noc_translation_enabled,
    const size_t queue_index,
    const NocId noc_id) {
    const tt_xy_pair arc_core = blackhole::get_arc_core(noc_translation_enabled, noc_id == NocId::NOC1);

    uint32_t queue_info_addr = 0;
    arc_apb->read(&queue_info_addr, blackhole::SCRATCH_RAM_11, sizeof(queue_info_addr), arc_core, noc_id);
    UMD_ASSERT(
        queue_info_addr >= ARC_CSM_BASE && queue_info_addr < ARC_CSM_BASE + ARC_CSM_SIZE,
        error::RuntimeError,
        fmt::format(
            "The ARC firmware has not published its message queues: queue info address {:#x}.", queue_info_addr));

    // Word 0 is the ARC address of queue 0, word 1 the entries per queue and the number of queues.
    std::array<uint32_t, 2> queue_info = {};
    device_protocol->read_ctrl(
        queue_info.data(), arc_core, queue_info_addr, sizeof(queue_info), noc_id, RegisterAccessWidth::BITS32);
    const uint32_t entries = queue_info[1] & QUEUE_ENTRY_COUNT_MASK;
    const uint32_t queue_count = (queue_info[1] >> QUEUE_COUNT_SHIFT) & QUEUE_COUNT_MASK;
    UMD_ASSERT(
        queue_index < queue_count && entries > 0,
        error::RuntimeError,
        fmt::format(
            "ARC message queue {} is not available: the firmware publishes {} queues of {} entries.",
            queue_index,
            queue_count,
            entries));

    const uint64_t queue_size_bytes = ARC_MSG_QUEUE_HEADER_SIZE + 2 * entries * ARC_QUEUE_ENTRY_SIZE;
    const uint64_t queue_base = queue_info[0] + queue_index * queue_size_bytes;
    UMD_ASSERT(
        queue_info[0] >= ARC_CSM_BASE && queue_base + queue_size_bytes <= ARC_CSM_BASE + ARC_CSM_SIZE,
        error::RuntimeError,
        fmt::format(
            "The ARC firmware publishes message queues outside its CSM: queue {} at {:#x}, {} bytes.",
            queue_index,
            queue_base,
            queue_size_bytes));
    log_debug(LogUMD, "Using ARC message queue {} at {:#x} with {} entries.", queue_index, queue_base, entries);
    return std::make_unique<BlackholeArcMessageQueue>(
        device_protocol, arc_apb, queue_base, entries, noc_translation_enabled);
}

void BlackholeArcMessageQueue::push_request(
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len>& request,
    const std::chrono::milliseconds timeout_ms,
    const NocId noc_id) {
    const uint32_t request_wptr = read_word(request_wptr_offset, noc_id);

    // Responses left behind by a request that timed out earlier would be taken for this request's.
    const uint32_t response_wptr = read_word(response_wptr_offset, noc_id);
    if (read_word(response_rptr_offset, noc_id) != response_wptr) {
        log_debug(LogUMD, "Discarding stale ARC message responses.");
        uint32_t new_rptr = response_wptr;
        write_words(&new_rptr, 1, response_rptr_offset, noc_id);
    }

    const bool has_space = poll_until(
        [&]() {
            const uint32_t occupied = (request_wptr - read_word(request_rptr_offset, noc_id)) % (2 * size);
            return occupied < size;
        },
        timeout_ms);
    UMD_ASSERT(has_space, error::RuntimeError, "Timed out waiting for space in the ARC message queue.");

    const size_t slot_offset = header_len + (request_wptr % size) * entry_len;
    write_words(request.data(), entry_len, slot_offset, noc_id);

    // The firmware takes the request once it sees the pointer move, so the pointer goes last.
    uint32_t next_wptr = (request_wptr + 1) % (2 * size);
    write_words(&next_wptr, 1, request_wptr_offset, noc_id);

    trigger_fw_int(noc_id);
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::pop_response(
    const std::chrono::milliseconds timeout_ms, const NocId noc_id) {
    const uint32_t response_rptr = read_word(response_rptr_offset, noc_id);

    const bool has_response =
        poll_until([&]() { return read_word(response_wptr_offset, noc_id) != response_rptr; }, timeout_ms);
    UMD_ASSERT(
        has_response,
        error::RuntimeError,
        fmt::format("Timed out after {} ms waiting for an ARC message response.", timeout_ms.count()));

    std::array<uint32_t, entry_len> response = {};
    const size_t slot_offset = header_len + (size + response_rptr % size) * entry_len;
    read_words(response.data(), entry_len, slot_offset, noc_id);

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
    uint32_t value = 0;
    read_words(&value, 1, offset, noc_id);
    // A hung NOC reads all ones, which is never a valid queue pointer.
    UMD_ASSERT(
        value != HANG_READ_VALUE,
        error::RuntimeError,
        "Reading the ARC message queue returned all ones; the device is not responding.");
    return value;
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
    const uint32_t doorbell = 0;
    arc_apb->write(&doorbell, ARC_MSI_CATCHER_OFFSET, sizeof(doorbell), get_arc_core(noc_id), noc_id);
}

tt_xy_pair BlackholeArcMessageQueue::get_arc_core(const NocId noc_id) const {
    return blackhole::get_arc_core(noc_translation_enabled, noc_id == NocId::NOC1);
}

}  // namespace tt::umd
