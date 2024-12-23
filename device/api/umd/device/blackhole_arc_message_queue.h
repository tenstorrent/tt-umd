/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/blackhole_implementation.h"
#include "umd/device/tt_core_coordinates.h"
#include "umd/device/types/blackhole_arc.h"
#include "umd/device/types/cluster_descriptor_types.h"

using namespace tt::umd::blackhole;

namespace tt::umd {

class Cluster;

class BlackholeArcMessageQueue {
private:
    // Header length and entry length in words.
    static constexpr uint8_t header_len = 8;
    static constexpr uint8_t entry_len = 8;

    static constexpr uint8_t request_wptr_offset = 0;
    static constexpr uint8_t response_rptr_offset = 1;
    static constexpr uint8_t request_rptr_offset = 4;
    static constexpr uint8_t response_wptr_offset = 5;

public:
    BlackholeArcMessageQueue(
        Cluster* cluster,
        const chip_id_t chip,
        const uint64_t base_address,
        const uint64_t size,
        const CoreCoord arc_core);

    uint32_t send_message(const ArcMessageType message_type, uint16_t arg0 = 0, uint16_t arg1 = 0);

    static std::shared_ptr<BlackholeArcMessageQueue> get_blackhole_arc_message_queue(
        Cluster* cluster, const chip_id_t chip, const size_t queue_index);

private:
    void push_request(std::array<uint32_t, BlackholeArcMessageQueue::entry_len>& request);

    std::array<uint32_t, entry_len> pop_response();

    void read_words(uint32_t* data, size_t num_words, size_t offset);

    uint32_t read_word(size_t offset);

    void write_words(uint32_t* data, size_t num_words, size_t offset);

    void create_request(uint32_t* request, ArcMessageType message_type, uint32_t* data, size_t num_words);

    void trigger_fw_int();

    const uint64_t base_address;
    const uint64_t size;
    Cluster* cluster;
    const chip_id_t chip;
    const CoreCoord arc_core;
};

}  // namespace tt::umd
