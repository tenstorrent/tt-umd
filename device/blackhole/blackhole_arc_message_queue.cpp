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
        data, core, base_address + offset * sizeof(uint32_t), num_words * sizeof(uint32_t), "LARGE_READ_TLB");
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
        data, num_words * sizeof(uint32_t), core, base_address + offset * sizeof(uint32_t), "LARGE_WRITE_TLB");
}

void BlackholeArcMessageQueue::create_request(
    uint32_t* request, ArcMessageType message_type, uint32_t* data, size_t num_words) {
    request[0] = (uint32_t)message_type;
    memcpy(request + 1, data, num_words * sizeof(uint32_t));
    memset(request + 1 + num_words, 0, (BlackholeArcMessageQueue::entry_len - (1 + num_words)) * sizeof(uint32_t));
}

void BlackholeArcMessageQueue::push_request(uint32_t* request) {
    std::cout << "push request" << std::endl;
    // TODO: add check for the length of the request.
    if (sizeof(request) != BlackholeArcMessageQueue::entry_len) {
        throw std::runtime_error(
            "ARC request length must be 32 bytes, invalid request lenght is " + std::to_string(sizeof(request)));
    }

    uint32_t request_queue_wptr = read_word(request_wptr_offset);

    std::cout << "request_queue_wptr " << request_queue_wptr << std::endl;

    while (true) {
        uint32_t request_queue_rptr = read_word(request_rptr_offset);
        if (abs((int)request_queue_rptr - (int)request_queue_wptr) % (2 * size) != size) {
            std::cout << "request_queue_rptr " << request_queue_rptr << std::endl;
            break;
        }

        // TODO: check for timeout.
    }

    // Offset in words.
    uint32_t request_entry_offset = header_len + (request_queue_wptr % size) * BlackholeArcMessageQueue::entry_len;
    std::cout << "request entry offset " << request_entry_offset << std::endl;
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

uint32_t BlackholeArcMessageQueue::send_message(
    const ArcMessageType message_type, uint16_t arg0, uint16_t arg1, uint32_t timeout) {
    uint32_t arg = arg0 | (arg1 << 16);

    uint32_t request[] = {(uint32_t)message_type, arg, 0, 0, 0, 0, 0, 0};

    push_request(request);

    std::array<uint32_t, BlackholeArcMessageQueue::entry_len> response = pop_response();

    uint32_t status = response[0] & 0xFF;

    if (status < 240) {
        return response[0] >> 16;
    } else if (status == 0xFF) {
        // TODO: report error
        return 0;
    } else {
        // TODO: report error
        return 0;
    }
}

#define MSG_QUEUE_HEADER_SIZE 32
#define QUEUE_ENTRY_SIZE 32

std::shared_ptr<BlackholeArcMessageQueue> BlackholeArcMessageQueue::get_blackhole_arc_message_queue(
    Cluster* cluster, const chip_id_t chip, const size_t queue_index) {
    const uint64_t queue_control_block_addr = blackhole::QUEUE_CONTROL_BLOCK_ADDR;

    tt_cxy_pair arc_core = {(size_t)chip, 8, 0};
    uint64_t queue_control_block;
    cluster->read_from_device(
        &queue_control_block, arc_core, queue_control_block_addr, sizeof(uint64_t), "LARGE_READ_TLB");

    std::cout << "queue_control_block: " << queue_control_block << std::endl;

    uint32_t queue_base_addr = queue_control_block & 0xFFFFFFFF;
    uint32_t num_entries_per_queue = (queue_control_block >> 32) & 0xFF;
    uint32_t num_queues = (queue_control_block >> 40) & 0xFF;

    uint32_t msg_queue_size = 2 * num_entries_per_queue * QUEUE_ENTRY_SIZE + MSG_QUEUE_HEADER_SIZE;
    uint32_t msg_queue_base = queue_base_addr + queue_index * msg_queue_size;

    std::cout << "queue_base_addr: " << queue_base_addr << std::endl;
    std::cout << "num_entries_per_queue: " << num_entries_per_queue << std::endl;
    std::cout << "num_queues: " << num_queues << std::endl;

    std::cout << "msg queue base " << msg_queue_base << std::endl;

    return std::make_shared<tt::umd::BlackholeArcMessageQueue>(cluster, chip, msg_queue_base, num_entries_per_queue);
}

}  // namespace tt::umd
