// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "pcie/io_window_reconfigure.hpp"

#include <cstddef>
#include <cstdint>

#include "common/unsupported.hpp"

namespace tt::umd {

void read_block_reconfigure(
    IoWindow& window, void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, IoOrdering ordering) {
    UMD_NOT_IMPLEMENTED();
}

void write_block_reconfigure(
    IoWindow& window,
    const void* mem_ptr,
    tt_xy_pair core,
    uint64_t addr,
    size_t size,
    NocId noc_id,
    IoOrdering ordering) {
    UMD_NOT_IMPLEMENTED();
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
    UMD_NOT_IMPLEMENTED();
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
    UMD_NOT_IMPLEMENTED();
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
    UMD_NOT_IMPLEMENTED();
}

}  // namespace tt::umd
