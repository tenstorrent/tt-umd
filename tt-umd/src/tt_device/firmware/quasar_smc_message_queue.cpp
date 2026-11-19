// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/firmware/quasar_smc_message_queue.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd-utils/internal/polling.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Registers, as offsets from the SMC's register block.
constexpr uint64_t COLD_SCRATCH_3_OFFSET = 0x280C;
// Mailbox TX channel 0 write-data, which the host writes after queuing a request to signal the firmware.
constexpr uint64_t DOORBELL_OFFSET = 0x18000;

// The firmware publishes SMC-local addresses, which carry the SMC's own 0xC0000000 window in the
// top bits; the rest is the offset into the register block the host reaches by SPA.
constexpr uint32_t SMC_LOCAL_OFFSET_MASK = 0x0FFFFFFF;

// Queue layout, as offsets from the queue header.
constexpr uint64_t REQUEST_WPTR_OFFSET = 0x00;
constexpr uint64_t RESPONSE_RPTR_OFFSET = 0x04;
constexpr uint64_t REQUEST_RPTR_OFFSET = 0x10;
constexpr uint64_t RESPONSE_WPTR_OFFSET = 0x14;
constexpr uint64_t REQUEST_SLOTS_OFFSET = 0x20;
constexpr uint64_t RESPONSE_SLOTS_OFFSET = 0xA0;

constexpr uint32_t SLOT_COUNT = 4;
constexpr uint64_t SLOT_SIZE = QuasarSmcMessageQueue::MESSAGE_WORDS * sizeof(uint32_t);
// Pointers run over twice the slot count so that a full ring can be told apart from an empty one.
constexpr uint32_t POINTER_MODULUS = 2 * SLOT_COUNT;

// The SMC is addressed by SPA alone, which the protocol serves on NOC0 with no core coordinate.
constexpr tt_xy_pair NO_CORE{0, 0};

uint32_t read_word(DeviceProtocol* device_protocol, uint64_t addr) {
    uint32_t word = 0;
    device_protocol->read_ctrl(&word, NO_CORE, addr, sizeof(word), NocId::NOC0, RegisterAccessWidth::BITS32);
    return word;
}

void write_word(DeviceProtocol* device_protocol, uint64_t addr, uint32_t word) {
    device_protocol->write_ctrl(&word, NO_CORE, addr, sizeof(word), NocId::NOC0, RegisterAccessWidth::BITS32);
}

uint64_t slot_address(uint64_t queue, uint64_t slots_offset, uint32_t pointer) {
    return queue + slots_offset + (pointer % SLOT_COUNT) * SLOT_SIZE;
}

}  // namespace

QuasarSmcMessageQueue::QuasarSmcMessageQueue(DeviceProtocol* device_protocol, uint64_t smc_base) :
    device_protocol_(device_protocol), smc_base_(smc_base) {
    UMD_ASSERT(device_protocol_ != nullptr, error::RuntimeError, "QuasarSmcMessageQueue requires a DeviceProtocol.");
}

std::array<uint32_t, QuasarSmcMessageQueue::MESSAGE_WORDS> QuasarSmcMessageQueue::send_message(
    uint32_t header, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout) {
    UMD_ASSERT(
        args.size() < MESSAGE_WORDS,
        error::RuntimeError,
        fmt::format("SMC messages take at most {} arguments, but {} were given.", MESSAGE_WORDS - 1, args.size()));

    std::array<uint32_t, MESSAGE_WORDS> request{header};
    std::copy(args.begin(), args.end(), request.begin() + 1);

    // Looked up on every call rather than cached: nothing tells this object when the firmware has
    // published the queue, or whether the pointers survive an SMC reset.
    const uint32_t queue_info_pointer = read_word(device_protocol_, smc_base_ + COLD_SCRATCH_3_OFFSET);
    UMD_ASSERT(
        queue_info_pointer != 0,
        error::RuntimeError,
        fmt::format("The SMC at 0x{:x} has not published its message queue.", smc_base_));
    const uint32_t queue_pointer =
        read_word(device_protocol_, smc_base_ + (queue_info_pointer & SMC_LOCAL_OFFSET_MASK));
    UMD_ASSERT(
        queue_pointer != 0,
        error::RuntimeError,
        fmt::format("The SMC at 0x{:x} has published no message queue in its queue info block.", smc_base_));
    const uint64_t queue = smc_base_ + (queue_pointer & SMC_LOCAL_OFFSET_MASK);

    // Responses are not tagged with the request they answer. Dropping those already waiting keeps
    // a timed-out caller's response from shifting every later caller's result by one.
    const uint32_t response_wptr = read_word(device_protocol_, queue + RESPONSE_WPTR_OFFSET);
    uint32_t response_rptr = read_word(device_protocol_, queue + RESPONSE_RPTR_OFFSET);
    if (response_rptr != response_wptr) {
        log_debug(LogUMD, "Discarding stale responses in the message queue of the SMC at 0x{:x}.", smc_base_);
        response_rptr = response_wptr;
        write_word(device_protocol_, queue + RESPONSE_RPTR_OFFSET, response_rptr);
    }

    // Only the host writes the request write pointer, so it is read once; the firmware advances the
    // read pointer as it takes requests.
    uint32_t request_wptr = read_word(device_protocol_, queue + REQUEST_WPTR_OFFSET);
    const auto start = std::chrono::steady_clock::now();
    while ((request_wptr - read_word(device_protocol_, queue + REQUEST_RPTR_OFFSET)) % POINTER_MODULUS >= SLOT_COUNT) {
        utils::check_timeout(
            start,
            timeout,
            fmt::format("Timed out waiting for a free request slot in the SMC message queue at 0x{:x}.", queue));
    }

    device_protocol_->write_ctrl(
        request.data(),
        NO_CORE,
        slot_address(queue, REQUEST_SLOTS_OFFSET, request_wptr),
        SLOT_SIZE,
        NocId::NOC0,
        RegisterAccessWidth::BITS32);
    request_wptr = (request_wptr + 1) % POINTER_MODULUS;
    write_word(device_protocol_, queue + REQUEST_WPTR_OFFSET, request_wptr);

    // The doorbell takes a 64-bit zero, issued as two 32-bit writes, low word first.
    const uint64_t doorbell_value = 0;
    device_protocol_->write_ctrl(
        &doorbell_value,
        NO_CORE,
        smc_base_ + DOORBELL_OFFSET,
        sizeof(doorbell_value),
        NocId::NOC0,
        RegisterAccessWidth::BITS32);

    while (read_word(device_protocol_, queue + RESPONSE_WPTR_OFFSET) == response_rptr) {
        utils::check_timeout(
            start,
            timeout,
            fmt::format("Timed out waiting for the SMC at 0x{:x} to answer message 0x{:x}.", smc_base_, header));
    }

    std::array<uint32_t, MESSAGE_WORDS> response{};
    device_protocol_->read_ctrl(
        response.data(),
        NO_CORE,
        slot_address(queue, RESPONSE_SLOTS_OFFSET, response_rptr),
        SLOT_SIZE,
        NocId::NOC0,
        RegisterAccessWidth::BITS32);
    write_word(device_protocol_, queue + RESPONSE_RPTR_OFFSET, (response_rptr + 1) % POINTER_MODULUS);

    return response;
}

}  // namespace tt::umd
