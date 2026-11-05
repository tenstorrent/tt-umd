// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <exception>
#include <vector>

#include "tt-umd/chip_helpers/host_mem_channels.hpp"

using namespace tt::umd;

TEST(ApiHostMemChannels, CopiesThroughMappedChannel) {
    std::vector<uint8_t> channel_memory(64, 0);
    HostMemChannels channels(2);
    channels.map_channel(1, channel_memory.data(), channel_memory.size());

    const std::vector<uint8_t> data_write = {1, 2, 3, 4};
    channels.write_to_channel(1, data_write.data(), 8, data_write.size());
    std::vector<uint8_t> data_read(data_write.size(), 0);
    channels.read_from_channel(1, data_read.data(), 8, data_read.size());

    EXPECT_EQ(data_read, data_write);
    EXPECT_EQ(channel_memory[8], 1);
    EXPECT_EQ(channels.get_channel_size(1), 64u);
}

TEST(ApiHostMemChannels, UnmappedChannelRejectsCopies) {
    std::vector<uint8_t> channel_memory(64, 0);
    HostMemChannels channels(1);
    channels.map_channel(0, channel_memory.data(), channel_memory.size());
    channels.unmap_channels();

    uint8_t byte = 0;
    EXPECT_EQ(channels.get_num_channels(), 1u);
    EXPECT_EQ(channels.get_channel_address(0), nullptr);
    EXPECT_THROW(channels.write_to_channel(0, &byte, 0, 1), std::exception);
    EXPECT_THROW(channels.read_from_channel(0, &byte, 0, 1), std::exception);
}
