/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/blackhole_arc_message_queue.h"

#include "umd/device/cluster.h"

using namespace tt::umd;

namespace tt::umd {

BlackholeArcMessageQueue::BlackholeArcMessageQueue(
    Cluster* cluster, const chip_id_t chip, const uint64_t base_address, const uint64_t size) :
    base_address(base_address), size(size), cluster(cluster), chip(chip) {}

void BlackholeArcMessageQueue::read_words(uint32_t* data, size_t num_words, size_t offset) {
    const CoreCoord arc_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::ARC)[0];
    tt_cxy_pair core = {(size_t)chip, arc_core.x, arc_core.y};
    cluster->read_from_device(
        data, core, base_address + offset * sizeof(uint32_t), num_words * sizeof(uint32_t), "LARGE_READ_WRITE_TLB");
}

uint32_t BlackholeArcMessageQueue::read_word(size_t offset) {
    uint32_t word;
    read_words(&word, 1, offset);
    return word;
}

void BlackholeArcMessageQueue::write_words(uint32_t* data, size_t num_words, size_t offset) {
    const CoreCoord arc_core = cluster->get_soc_descriptor(chip).get_cores(CoreType::ARC)[0];
    tt_cxy_pair core = {(size_t)chip, arc_core.x, arc_core.y};
    cluster->write_to_device(
        data, num_words * sizeof(uint32_t), core, base_address + offset * sizeof(uint32_t), "LARGE_READ_WRITE_TLB");
}

void BlackholeArcMessageQueue::create_request(
    uint32_t* request, ArcMessageType message_type, uint32_t* data, size_t num_words) {
    request[0] = (uint32_t)message_type;
    memcpy(request + 1, data, num_words * sizeof(uint32_t));
    memset(request + 1 + num_words, 0, (BlackholeArcMessageQueue::entry_len - (1 + num_words)) * sizeof(uint32_t));
}

void BlackholeArcMessageQueue::push_request(uint32_t* request) {
    // TODO: add check for the length of the request.
    if (sizeof(request) != BlackholeArcMessageQueue::entry_len) {
        throw std::runtime_error(
            "ARC request length must be 32 bytes, invalid request lenght is " + std::to_string(sizeof(request)));
    }

    uint32_t request_queue_wptr = read_word(request_wptr_offset);

    while (true) {
        uint32_t request_queue_rptr = read_word(request_rptr_offset);
        if (abs(request_queue_rptr - request_queue_wptr) % (2 * size) != size) {
            break;
        }

        // TODO: check for timeout.
    }

    // Offset in words.
    uint32_t request_entry_offset = header_len + (request_queue_wptr % size) * BlackholeArcMessageQueue::entry_len;
    write_words(request, BlackholeArcMessageQueue::entry_len, request_entry_offset);

    request_queue_wptr = (request_queue_wptr + 1) % (2 * size);
    write_words(&request_queue_wptr, 1, request_wptr_offset);

    // TODO: what is trigger fw init?
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::pop_response() {
    uint32_t response_queue_rptr = read_word(response_rptr_offset);

    while (true) {
        uint32_t response_queue_wptr = read_word(response_wptr_offset);

        if (response_queue_rptr != response_queue_wptr) {
            break;
        }

        // TODO: add timeout.
    }

    uint32_t response_entry_offset =
        header_len + (size + (response_queue_rptr % size)) * BlackholeArcMessageQueue::entry_len;
    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> response;
    read_words(response.data(), BlackholeArcMessageQueue::entry_len, response_entry_offset);

    response_queue_rptr = (response_queue_rptr + 1) % (2 * size);
    write_words(&response_queue_rptr, 1, response_rptr_offset);

    return response;
}

std::array<uint32_t, BlackholeArcMessageQueue::entry_len> BlackholeArcMessageQueue::send_message(
    ArcMessageType message_type, uint32_t* data, size_t num_words) {
    uint32_t request[BlackholeArcMessageQueue::entry_len];
    create_request(request, message_type, data, num_words);
    push_request(request);
    return pop_response();
}

}  // namespace tt::umd
