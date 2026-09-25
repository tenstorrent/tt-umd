// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/io_window_reconfigure.hpp"

#include <algorithm>
#include <optional>

namespace tt::umd {

namespace {

// Each chunk is configured immediately before it is transferred, so the mapping only ever carries one
// direction of traffic and says so. The direction follows from the transfer rather than from the caller.
template <typename buffer_pointer, typename io_operation>
void transfer_and_reconfigure(
    IoWindow& window,
    TargetIoWindowConfig config,
    IoOrdering ordering,
    buffer_pointer buffer,
    size_t size,
    io_operation op) {
    while (size > 0) {
        window.configure(config, ordering);
        size_t transfer_size = std::min(size, window.get_size());
        op(buffer, transfer_size);
        size -= transfer_size;
        config.addr += transfer_size;
        buffer += transfer_size;
    }
}

}  // namespace

void read_block_reconfigure(
    IoWindow& window, void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, IoOrdering ordering) {
    transfer_and_reconfigure(
        window,
        TargetIoWindowConfig{core, std::nullopt, addr, noc_id, WindowFlags::UnicastRead},
        ordering,
        static_cast<uint8_t*>(mem_ptr),
        size,
        [&window](uint8_t* buf, size_t sz) { window.read_block(0, buf, sz); });
}

void write_block_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering) {
    transfer_and_reconfigure(
        window,
        TargetIoWindowConfig{core, std::nullopt, addr, noc_id, WindowFlags::UnicastWrite},
        ordering,
        static_cast<const uint8_t*>(mem_ptr),
        size,
        [&window](const uint8_t* buf, size_t sz) { window.write_block(0, buf, sz); });
}

void read_register_reconfigure(
    IoWindow& window, void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, IoOrdering ordering) {
    transfer_and_reconfigure(
        window,
        TargetIoWindowConfig{core, std::nullopt, addr, noc_id, WindowFlags::UnicastRead},
        ordering,
        static_cast<uint8_t*>(mem_ptr),
        size,
        [&window](uint8_t* buf, size_t sz) { window.read_aligned(0, buf, sz); });
}

void write_register_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering) {
    transfer_and_reconfigure(
        window,
        TargetIoWindowConfig{core, std::nullopt, addr, noc_id, WindowFlags::UnicastWrite},
        ordering,
        static_cast<const uint8_t*>(mem_ptr),
        size,
        [&window](const uint8_t* buf, size_t sz) { window.write_aligned(0, buf, sz); });
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
    transfer_and_reconfigure(
        window,
        TargetIoWindowConfig{core_start, core_end, addr, noc_id, WindowFlags::MulticastWrite},
        ordering,
        static_cast<const uint8_t*>(src),
        size,
        [&window](const uint8_t* buf, size_t sz) { window.write_block(0, buf, sz); });
}

}  // namespace tt::umd
