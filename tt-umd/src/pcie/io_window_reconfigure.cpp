// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/io_window_reconfigure.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "common/utils.hpp"

namespace tt::umd {

namespace {

// Points the window at the next chunk of a transfer and returns how much of the remaining bytes it reaches.
size_t configure_chunk(
    IoWindow& window,
    tt_xy_pair core_start,
    std::optional<tt_xy_pair> core_end,
    uint64_t addr,
    size_t remaining,
    NocId noc_id,
    WindowFlags direction,
    IoOrdering ordering) {
    window.configure(
        TargetIoWindowConfig{
            .core_start = core_start, .core_end = core_end, .addr = addr, .noc = noc_id, .flags = direction},
        ordering);
    return std::min(remaining, window.get_size());
}

// Moves one register through the window at the given offset, at the register's own width.
void read_register_at(IoWindow& window, uint64_t offset, uint8_t* dst, RegisterAccessWidth width) {
    if (width == RegisterAccessWidth::BITS64) {
        const uint64_t value = window.read64(offset);
        std::memcpy(dst, &value, sizeof(value));
    } else {
        const uint32_t value = window.read32(offset);
        std::memcpy(dst, &value, sizeof(value));
    }
}

void write_register_at(IoWindow& window, uint64_t offset, const uint8_t* src, RegisterAccessWidth width) {
    if (width == RegisterAccessWidth::BITS64) {
        uint64_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        window.write64(offset, value);
    } else {
        uint32_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        window.write32(offset, value);
    }
}

}  // namespace

void read_block_reconfigure(
    IoWindow& window, void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, IoOrdering ordering) {
    auto* dst = static_cast<uint8_t*>(mem_ptr);
    while (size > 0) {
        const size_t chunk =
            configure_chunk(window, core, std::nullopt, addr, size, noc_id, WindowFlags::UnicastRead, ordering);
        window.read_block(0, dst, chunk);
        dst += chunk;
        addr += chunk;
        size -= chunk;
    }
}

void write_block_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering) {
    const auto* src = static_cast<const uint8_t*>(mem_ptr);
    while (size > 0) {
        const size_t chunk =
            configure_chunk(window, core, std::nullopt, addr, size, noc_id, WindowFlags::UnicastWrite, ordering);
        window.write_block(0, src, chunk);
        src += chunk;
        addr += chunk;
        size -= chunk;
    }
}

void read_register_reconfigure(
    IoWindow& window,
    void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    RegisterAccessWidth width,
    IoOrdering ordering) {
    const size_t access_size = register_access_size(width);
    // A partial register at the end would overrun the caller's buffer.
    validate_register_access(addr, size, width);
    auto* dst = static_cast<uint8_t*>(mem_ptr);
    while (size > 0) {
        // A window spans a multiple of every register width, so an aligned register never straddles two chunks.
        const size_t chunk =
            configure_chunk(window, core, std::nullopt, addr, size, noc_id, WindowFlags::UnicastRead, ordering);
        for (size_t offset = 0; offset < chunk; offset += access_size) {
            read_register_at(window, offset, dst + offset, width);
        }
        dst += chunk;
        addr += chunk;
        size -= chunk;
    }
}

void write_register_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    RegisterAccessWidth width,
    IoOrdering ordering) {
    const size_t access_size = register_access_size(width);
    validate_register_access(addr, size, width);
    const auto* src = static_cast<const uint8_t*>(mem_ptr);
    while (size > 0) {
        const size_t chunk =
            configure_chunk(window, core, std::nullopt, addr, size, noc_id, WindowFlags::UnicastWrite, ordering);
        for (size_t offset = 0; offset < chunk; offset += access_size) {
            write_register_at(window, offset, src + offset, width);
        }
        src += chunk;
        addr += chunk;
        size -= chunk;
    }
}

void noc_multicast_write_reconfigure(
    IoWindow& window,
    const void* src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    IoOrdering ordering) {
    const auto* src_bytes = static_cast<const uint8_t*>(src);
    while (size > 0) {
        const size_t chunk =
            configure_chunk(window, core_start, core_end, addr, size, noc_id, WindowFlags::MulticastWrite, ordering);
        window.write_block(0, src_bytes, chunk);
        src_bytes += chunk;
        addr += chunk;
        size -= chunk;
    }
}

}  // namespace tt::umd
