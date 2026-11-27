// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/chip_helpers/host_mem_channels.hpp"

#include <fmt/format.h>

#include <cstring>
#include <tt-logger/tt-logger.hpp>

#include "tt-umd-utils/internal/tracy.hpp"
#include "tt-umd/chip_helpers/host_cache.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// A platform that does not snoop inbound DMA and cannot issue cache-maintenance instructions has no
// correct way to share sysmem with the device. Nothing here can fix that, so say so once and loudly
// rather than let it look like it worked.
void warn_once_if_maintenance_unavailable() {
    static const bool reported = [] {
        if (host_cache::maintenance_required_but_unavailable()) {
            log_error(
                LogUMD,
                "Host cache maintenance is required on this platform but unavailable: the interconnect "
                "does not report snooping this device's inbound DMA, and no usable cache-maintenance "
                "instruction was found. Sysmem shared with the device may return stale data in either "
                "direction.");
        }
        return true;
    }();
    (void)reported;
}

}  // namespace

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
    UMD_ASSERT(
        size <= CHANNEL_STRIDE,
        error::RuntimeError,
        fmt::format("Host memory channel {} of {} bytes overlaps the next channel.", channel, size));
    channels_[channel] = {address, size};
}

void HostMemChannels::unmap_channels() {
    for (Channel& channel : channels_) {
        channel = {};
    }
}

void HostMemChannels::write_to_channel(uint16_t channel, const void* src, uint64_t offset, size_t size) const {
    warn_once_if_maintenance_unavailable();
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
    char* const dest = static_cast<char*>(address) + (offset % channel_size);
    memcpy(dest, src, size);

    // The device DMA-reads this back out of DRAM, so the host's stores have to be written back on a
    // platform that does not snoop. No-op where it does.
    host_cache::clean_range(dest, size);
}

void HostMemChannels::read_from_channel(uint16_t channel, void* dest, uint64_t offset, size_t size) const {
    warn_once_if_maintenance_unavailable();
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
    const char* const src = static_cast<const char*>(address) + (offset % channel_size);
    // The device DMA-wrote this, so drop any cached lines first and let the copy refetch from DRAM.
    host_cache::invalidate_range(src, size);
    memcpy(dest, src, size);
}

}  // namespace tt::umd
