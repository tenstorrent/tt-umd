// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/host_mem_channels.hpp"

#include <fmt/format.h>

#include <cstring>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

HostMemChannels::HostMemChannels(size_t num_channels) : channels_(num_channels) {}

void* HostMemChannels::get_channel_address(size_t channel) const {
    return channel < channels_.size() ? channels_[channel].address : nullptr;
}

size_t HostMemChannels::get_channel_size(size_t channel) const {
    return channel < channels_.size() ? channels_[channel].size : 0;
}

void HostMemChannels::map_channel(size_t channel, void* address, size_t size) {
    UMD_ASSERT(
        channel < channels_.size(),
        error::RuntimeError,
        fmt::format("Host memory channel {} does not exist; there are {}.", channel, channels_.size()));
    UMD_ASSERT(address != nullptr && size != 0, error::RuntimeError, "A host memory channel needs memory to map.");
    channels_[channel] = {address, size};
}

void HostMemChannels::unmap_channels() {
    for (Channel& channel : channels_) {
        channel = {};
    }
}

void HostMemChannels::write_to_channel(uint16_t channel, const void* src, uint64_t offset, uint32_t size) {
    ZoneScopedC(tracy::Color::Yellow);
    void* address = get_channel_address(channel);
    const size_t channel_size = get_channel_size(channel);
    UMD_ASSERT(
        address,
        error::RuntimeError,
        fmt::format(
            "write_buffer: Hugepages are not allocated ch: {}."
            " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
            channel));
    UMD_ASSERT(
        size <= channel_size,
        error::RuntimeError,
        fmt::format("write_buffer data has larger size {} than destination buffer {}", size, channel_size));
    log_debug(
        LogUMD,
        "Using hugepage mapping at address {:p} offset {} chan {} size {}",
        address,
        (offset % channel_size),
        channel,
        size);
    memcpy(static_cast<char*>(address) + (offset % channel_size), src, size);
}

void HostMemChannels::read_from_channel(uint16_t channel, void* dest, uint64_t offset, uint32_t size) const {
    ZoneScopedC(tracy::Color::Yellow);
    void* address = get_channel_address(channel);
    const size_t channel_size = get_channel_size(channel);
    UMD_ASSERT(
        address,
        error::RuntimeError,
        fmt::format(
            "read_buffer: Hugepages are not allocated ch: {}."
            " - Ensure sufficient number of Hugepages installed per device (1 per host mem ch, per device)",
            channel));
    memcpy(dest, static_cast<const char*>(address) + (offset % channel_size), size);
}

}  // namespace tt::umd
