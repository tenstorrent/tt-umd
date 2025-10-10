/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "umd/device/pcie/tlb_handle.hpp"

namespace tt::umd {

class TlbWindow {
public:
    TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config);

    void write32(uint64_t offset, uint32_t value);

    uint32_t read32(uint64_t offset);

    void write_register(uint64_t offset, uint32_t value);

    uint32_t read_register(uint64_t offset);

    void write_block(uint64_t offset, const void* data, size_t size);

    void read_block(uint64_t offset, void* data, size_t size);

    TlbHandle& handle_ref() const;

    size_t get_size() const;

    void configure(const tlb_data& new_config);

    uint64_t get_base_address() const;

private:
    void validate(uint64_t offset, size_t size) const;

    uint64_t get_total_offset(uint64_t offset) const;

    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    static void memcpy_from_device(void* dest, const void* src, std::size_t num_bytes);
    static void memcpy_to_device(void* dest, const void* src, std::size_t num_bytes);

    std::unique_ptr<TlbHandle> tlb_handle;
    uint64_t offset_from_aligned_addr = 0;
};

}  // namespace tt::umd
