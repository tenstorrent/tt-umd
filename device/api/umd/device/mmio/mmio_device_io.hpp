// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Abstract base class for device memory-mapped I/O operations.
 * Provides the same interface as TlbWindow for read/write operations.
 */
class MMIODeviceIO {
public:
    virtual ~MMIODeviceIO() = default;

    /**
     * Write a 32-bit value to the specified offset.
     */
    virtual void write32(uint64_t offset, uint32_t value) = 0;

    /**
     * Read a 32-bit value from the specified offset.
     */
    virtual uint32_t read32(uint64_t offset) = 0;

    /**
     * Write register data to the specified offset.
     */
    virtual void write_register(uint64_t offset, const void* data, size_t size) = 0;

    /**
     * Read register data from the specified offset.
     */
    virtual void read_register(uint64_t offset, void* data, size_t size) = 0;

    /**
     * Write a block of data to the specified offset.
     */
    virtual void write_block(uint64_t offset, const void* data, size_t size) = 0;

    /**
     * Read a block of data from the specified offset.
     */
    virtual void read_block(uint64_t offset, void* data, size_t size) = 0;

    /**
     * Read block with reconfiguration for specific core and address.
     */
    virtual void read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) = 0;

    /**
     * Write block with reconfiguration for specific core and address.
     */
    virtual void write_block_reconfigure(
        const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) = 0;

    /**
     * NOC multicast write with reconfiguration.
     */
    virtual void noc_multicast_write_reconfigure(
        void* dst,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint64_t ordering = tlb_data::Strict) = 0;

    /**
     * Get the size of the memory window.
     */
    virtual size_t get_size() const = 0;

    /**
     * Configure the memory mapping with new configuration.
     */
    virtual void configure(const tlb_data& new_config) = 0;

    /**
     * Get the base address of the memory window.
     */
    virtual uint64_t get_base_address() const = 0;

protected:
    /**
     * Validate offset and size parameters.
     */
    virtual void validate(uint64_t offset, size_t size) const = 0;
};

}  // namespace tt::umd