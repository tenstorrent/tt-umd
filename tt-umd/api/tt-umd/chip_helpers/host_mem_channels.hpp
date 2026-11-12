// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tt::umd {

/**
 * Host memory channels: numbered regions of host memory the device reaches over PCIe, channel N at NOC
 * address pcie_base + N GB.
 *
 * This is the host-side view of a chip's channels. The chip fixes how many there are and maps each one onto
 * memory it keeps alive for as long as the channel stays mapped. Copying through a channel that is not
 * mapped throws.
 */
class HostMemChannels {
public:
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

    void map_channel(size_t channel, void* address, size_t size);

    void unmap_channels();

    /**
     * Copies size bytes from src into the channel at offset, which wraps at the channel size.
     */
    void write_to_channel(uint16_t channel, const void* src, uint64_t offset, uint32_t size);

    /**
     * Copies size bytes from the channel at offset, which wraps at the channel size, into dest.
     */
    void read_from_channel(uint16_t channel, void* dest, uint64_t offset, uint32_t size) const;

private:
    struct Channel {
        void* address = nullptr;
        size_t size = 0;
    };

    std::vector<Channel> channels_;
};

}  // namespace tt::umd
