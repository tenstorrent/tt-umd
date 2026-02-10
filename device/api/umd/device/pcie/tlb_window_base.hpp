// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Base class for TlbWindow implementations that contains all shared logic.
 * The memory access methods are pure virtual to allow different implementations
 * for silicon (direct memory access) vs simulation (communicator-based access).
 */
class TlbWindowBase {
public:
    TlbWindowBase(std::unique_ptr<TlbHandle> handle, const tlb_data config = {});

    virtual ~TlbWindowBase() = default;

    // Pure virtual methods for memory access - to be implemented by derived classes.
    virtual void write32(uint64_t offset, uint32_t value) = 0;
    virtual uint32_t read32(uint64_t offset) = 0;
    virtual void write_register(uint64_t offset, const void* data, size_t size) = 0;
    virtual void read_register(uint64_t offset, void* data, size_t size) = 0;
    virtual void write_block(uint64_t offset, const void* data, size_t size) = 0;
    virtual void read_block(uint64_t offset, void* data, size_t size) = 0;

    // Shared higher-level methods that use the virtual methods above.
    void read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict);

    void write_block_reconfigure(
        const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict);

    void noc_multicast_write_reconfigure(
        void* dst,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint64_t ordering = tlb_data::Strict);

    // Shared utility methods.
    TlbHandle& handle_ref() const;
    size_t get_size() const;
    void configure(const tlb_data& new_config);
    uint64_t get_base_address() const;

protected:
    void validate(uint64_t offset, size_t size) const;
    uint64_t get_total_offset(uint64_t offset) const;

    std::unique_ptr<TlbHandle> tlb_handle;
    uint64_t offset_from_aligned_addr = 0;
};

}  // namespace tt::umd
