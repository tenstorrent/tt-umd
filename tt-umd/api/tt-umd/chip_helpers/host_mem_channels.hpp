// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tt::umd {

/**
 * Host memory channels: numbered regions of host memory the device can reach, channel N at NOC address
 * noc_base + N * CHANNEL_STRIDE.
 *
 * This is the host-side view of a chip's channels. The chip fixes how many there are and maps each one onto
 * memory it keeps alive for as long as the channel stays mapped. Copying through a channel that is not
 * mapped throws.
 */
class HostMemChannels {
public:
    // Most channels a chip may have.
    static constexpr uint32_t MAX_CHANNELS = 4;

    // Distance between consecutive channels on the NOC; no channel can be larger than one stride.
    static constexpr size_t CHANNEL_STRIDE = 1ULL << 30;

    explicit HostMemChannels(size_t num_channels = 0);

    size_t get_num_channels() const { return channels_.size(); }

    /**
     * @return Host address of the channel, or nullptr when it does not exist or is not mapped.
     */
    void* get_channel_address(size_t channel) const;

    /**
     * @return Size of the channel in bytes, or 0 when it does not exist or is not mapped.
     */
    size_t get_channel_size(size_t channel) const;

    /**
     * Maps channel onto memory the caller keeps alive while the channel is mapped. Throws if the channel does not
     * exist, the memory is null or empty, or it is larger than CHANNEL_STRIDE.
     */
    void map_channel(size_t channel, void* address, size_t size);

    void unmap_channels();

    /**
     * Copies size bytes from src into the channel at offset, which wraps at the channel size.
     */
    void write_to_channel(uint16_t channel, const void* src, uint64_t offset, size_t size) const;

    /**
     * Copies size bytes from the channel at offset, which wraps at the channel size, into dest.
     */
    void read_from_channel(uint16_t channel, void* dest, uint64_t offset, size_t size) const;

private:
    struct Channel {
        void* address = nullptr;
        size_t size = 0;
    };

    std::vector<Channel> channels_;
};

}  // namespace tt::umd
