// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/pcie/tlb_handle.hpp"

namespace tt::umd {

/**
 * Silicon TlbWindow implementation that performs direct memory access
 * using pointer dereferencing for accessing BAR0 mapped memory.
 */
class SiliconTlbWindow : public TlbWindow {
public:
    SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config = {});

    // Implementation of memory access methods using direct pointer access.
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write_register(uint64_t offset, const void* data, size_t size) override;
    void read_register(uint64_t offset, void* data, size_t size) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;

private:
    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    static void memcpy_from_device(void* dest, const void* src, std::size_t num_bytes);
    static void memcpy_to_device(void* dest, const void* src, std::size_t num_bytes);

    void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len);
    void read_regs(void* src_reg, uint32_t word_len, void* data);
};

}  // namespace tt::umd
