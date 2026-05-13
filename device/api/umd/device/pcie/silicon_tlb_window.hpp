// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class TlbHandle;

/**
 * Silicon TlbWindow implementation that performs direct memory access
 * using pointer dereferencing for accessing BAR0 mapped memory.
 */
class SiliconTlbWindow : public TlbWindow {
public:
    SiliconTlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config = {});

    // Implementation of memory access methods using direct pointer access.
    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;
    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;
    void write_register(uint64_t offset, const void* data, size_t size) override;
    void read_register(uint64_t offset, void* data, size_t size) override;
    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;

    void safe_write16(uint64_t offset, uint16_t value) override;

    uint16_t safe_read16(uint64_t offset) override;

    void safe_write32(uint64_t offset, uint32_t value) override;

    uint32_t safe_read32(uint64_t offset) override;

    void safe_write_register(uint64_t offset, const void* data, size_t size) override;

    void safe_read_register(uint64_t offset, void* data, size_t size) override;

    void safe_write_block(uint64_t offset, const void* data, size_t size) override;

    void safe_read_block(uint64_t offset, void* data, size_t size) override;

    void safe_write_block_reconfigure(
        const void* mem_ptr,
        tt_xy_pair core,
        uint64_t addr,
        size_t size,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict) override;

    void safe_read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering = tlb_data::Strict)
        override;

    void safe_noc_multicast_write_reconfigure(
        const void* src,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict) override;

    static void set_sigbus_safe_handler(bool set_safe_handler);

    /**
     * Configure the wall-clock budget enforced inside block-level MMIO
     * routines (`write_block`/`read_block`). Default is 1000 ms, overridable
     * at process start via the env var `TT_UMD_MMIO_TIMEOUT_MS`. This is a
     * process-wide setting — a NOC hang is device-wide and the same budget
     * applies to every window.
     *
     * On overrun the affected call throws `tt::umd::error::DeviceTimeoutError`.
     * The budget is best-effort, not a hard upper bound on wall time: the
     * check runs between bulk-loop iterations, so worst case is
     * `budget + PCIe completion timeout for one in-flight transaction`.
     */
    static void set_mmio_timeout_ms(uint32_t ms);
    static std::chrono::milliseconds get_mmio_timeout();

private:
    // Custom device memcpy. This is only safe for memory-like regions on the device (Tensix L1, DRAM, ARC CSM).
    // Both routines assume that misaligned accesses are permitted on host memory.
    //
    // 1. AARCH64 device memory does not allow unaligned accesses (including pair loads/stores),
    // which glibc's memcpy may perform when unrolling. This affects from and to device.
    // 2. syseng#3487 WH GDDR5 controller has a bug when 1-byte writes are temporarily adjacent
    // to 2-byte writes. We avoid ever performing a 1-byte write to the device. This only affects to device.
    static void memcpy_from_device(
        void* dest, const volatile void* src, std::size_t num_bytes, std::chrono::steady_clock::time_point deadline);
    static void memcpy_to_device(
        void* dest, const void* src, std::size_t num_bytes, std::chrono::steady_clock::time_point deadline);

    void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len);
    void read_regs(void* src_reg, uint32_t word_len, void* data);

    template <typename Func, typename... Args>
    decltype(auto) execute_safe(Func&& func, Args&&... args);
};

}  // namespace tt::umd
