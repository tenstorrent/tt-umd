// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

// Substitute for 1d_fabric_constants.hpp

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "umd/device/lite_fabric/lite_fabric_header.h"

namespace tt::umd {

namespace lite_fabric {

// STREAM REGISTER ASSIGNMENT
// senders update this stream
constexpr uint32_t to_receiver_0_pkts_sent_id = 0;
// senders update this stream
constexpr uint32_t to_receiver_1_pkts_sent_id = 1;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_0_pkts_acked_id = 2;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_1_pkts_acked_id = 3;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_2_pkts_acked_id = 4;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_3_pkts_acked_id = 5;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_4_pkts_acked_id = 6;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_0_pkts_completed_id = 7;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_1_pkts_completed_id = 8;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_2_pkts_completed_id = 9;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_3_pkts_completed_id = 10;
// receivers updates the reg on this stream
constexpr uint32_t to_sender_4_pkts_completed_id = 11;
constexpr uint32_t receiver_channel_0_free_slots_from_east_stream_id = 12;
constexpr uint32_t receiver_channel_0_free_slots_from_west_stream_id = 13;
constexpr uint32_t receiver_channel_0_free_slots_from_north_stream_id = 14;
constexpr uint32_t receiver_channel_0_free_slots_from_south_stream_id = 15;
constexpr uint32_t sender_channel_0_free_slots_stream_id = 17;
constexpr uint32_t sender_channel_1_free_slots_stream_id = 18;
constexpr uint32_t sender_channel_2_free_slots_stream_id = 19;
constexpr uint32_t sender_channel_3_free_slots_stream_id = 20;
constexpr uint32_t sender_channel_4_free_slots_stream_id = 21;
constexpr uint32_t vc1_sender_channel_free_slots_stream_id = 22;

constexpr size_t MAX_NUM_RECEIVER_CHANNELS = 2;

constexpr size_t MAX_NUM_SENDER_CHANNELS = 5;

// Only 1 receiver because 1 erisc
constexpr uint32_t NUM_RECEIVER_CHANNELS = 1;
constexpr uint32_t NUM_USED_RECEIVER_CHANNELS = 1;

// Only 1 sender because no upstream edm
constexpr uint32_t NUM_SENDER_CHANNELS = 1;

constexpr size_t VC1_SENDER_CHANNEL = NUM_SENDER_CHANNELS - 1;

constexpr uint8_t NUM_TRANSACTION_IDS = 4;

constexpr std::array<size_t, NUM_SENDER_CHANNELS> SENDER_NUM_BUFFERS_ARRAY = {2};

constexpr std::array<size_t, NUM_RECEIVER_CHANNELS> RECEIVER_NUM_BUFFERS_ARRAY = {2};

constexpr std::array<size_t, NUM_RECEIVER_CHANNELS> REMOTE_RECEIVER_NUM_BUFFERS_ARRAY = RECEIVER_NUM_BUFFERS_ARRAY;

static_assert(NUM_SENDER_CHANNELS == 1);

// Additional 16B to be used only for unaligned reads/writes
constexpr uint32_t CHANNEL_BUFFER_SIZE = 4096 + 16 + sizeof(lite_fabric::LiteFabricHeader);

constexpr size_t RECEIVER_CHANNEL_BASE_ID = NUM_SENDER_CHANNELS;
constexpr size_t SENDER_CHANNEL_BASE_ID = 0;
}  // namespace lite_fabric
}  // namespace tt::umd
