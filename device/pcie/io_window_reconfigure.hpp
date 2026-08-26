// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/io_window/io_window.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Chunked block read/write against an IoWindow: reconfigures the window to (core, addr) and
 * transfers up to window.get_size() bytes per iteration, advancing addr and the buffer pointer
 * until size is exhausted.
 *
 * Postcondition: the window is left configured to the last chunk transferred, not restored to
 * whatever it was configured to before the call.
 */
void read_block_reconfigure(
    IoWindow& window,
    void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering = IoOrdering::Strict);

void write_block_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering = IoOrdering::Strict);

// Register reconfigure functions transfer through read_aligned/write_aligned. Alignment
// enforcement is the caller's responsibility.
void read_register_reconfigure(
    IoWindow& window,
    void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering = IoOrdering::Strict);

void write_register_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering = IoOrdering::Strict);

void noc_multicast_write_reconfigure(
    IoWindow& window,
    const void* src,
    size_t size,
    tt_xy_pair core_start,
    tt_xy_pair core_end,
    uint64_t addr,
    NocId noc_id,
    IoOrdering ordering = IoOrdering::Strict);

}  // namespace tt::umd
