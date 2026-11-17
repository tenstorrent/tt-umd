// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/io_window_reconfigure.hpp"

#include <algorithm>
#include <cstring>
#include <optional>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// Points the window at (core_start[..core_end], addr) and returns how many bytes of the transfer it can take
// from there, never more than remaining and always a whole number of alignment units.
size_t configure_chunk(
    IoWindow& window,
    tt_xy_pair core_start,
    std::optional<tt_xy_pair> core_end,
    uint64_t addr,
    size_t remaining,
    size_t alignment,
    NocId noc_id,
    WindowFlags direction,
    IoOrdering ordering) {
    window.configure(TargetIoWindowConfig{core_start, core_end, addr, noc_id, direction}, ordering);
    const size_t chunk = std::min(remaining, window.get_size()) / alignment * alignment;
    if (chunk == 0) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "IoWindow of {} bytes at address {:#x} cannot fit a {}-byte access.",
                window.get_size(),
                addr,
                alignment));
    }
    return chunk;
}

size_t register_bytes(RegisterAccessWidth width) { return width == RegisterAccessWidth::BITS64 ? 8 : 4; }

}  // namespace

void read_block_reconfigure(
    IoWindow& window, void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, IoOrdering ordering) {
    auto* dst = static_cast<uint8_t*>(mem_ptr);
    while (size > 0) {
        const size_t chunk =
            configure_chunk(window, core, std::nullopt, addr, size, 1, noc_id, WindowFlags::UnicastRead, ordering);
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
            configure_chunk(window, core, std::nullopt, addr, size, 1, noc_id, WindowFlags::UnicastWrite, ordering);
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
    const size_t reg_size = register_bytes(width);
    auto* dst = static_cast<uint8_t*>(mem_ptr);
    while (size > 0) {
        const size_t chunk = configure_chunk(
            window, core, std::nullopt, addr, size, reg_size, noc_id, WindowFlags::UnicastRead, ordering);
        for (size_t offset = 0; offset < chunk; offset += reg_size) {
            if (width == RegisterAccessWidth::BITS64) {
                const uint64_t value = window.read64(offset);
                std::memcpy(dst + offset, &value, sizeof(value));
            } else {
                const uint32_t value = window.read32(offset);
                std::memcpy(dst + offset, &value, sizeof(value));
            }
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
    const size_t reg_size = register_bytes(width);
    const auto* src = static_cast<const uint8_t*>(mem_ptr);
    while (size > 0) {
        const size_t chunk = configure_chunk(
            window, core, std::nullopt, addr, size, reg_size, noc_id, WindowFlags::UnicastWrite, ordering);
        for (size_t offset = 0; offset < chunk; offset += reg_size) {
            if (width == RegisterAccessWidth::BITS64) {
                uint64_t value = 0;
                std::memcpy(&value, src + offset, sizeof(value));
                window.write64(offset, value);
            } else {
                uint32_t value = 0;
                std::memcpy(&value, src + offset, sizeof(value));
                window.write32(offset, value);
            }
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
    const auto* data = static_cast<const uint8_t*>(src);
    while (size > 0) {
        const size_t chunk =
            configure_chunk(window, core_start, core_end, addr, size, 1, noc_id, WindowFlags::MulticastWrite, ordering);
        window.write_block(0, data, chunk);
        data += chunk;
        addr += chunk;
        size -= chunk;
    }
}

}  // namespace tt::umd
