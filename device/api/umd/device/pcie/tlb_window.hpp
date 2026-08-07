// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Base class for TlbWindow implementations that contains all shared logic.
 * The memory access methods are pure virtual to allow different implementations
 * for silicon (direct memory access) vs simulation (communicator-based access).
 */
class TlbWindow {
public:
    TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config = {});

    virtual ~TlbWindow() = default;

    // Pure virtual methods for memory access - to be implemented by derived classes.
    virtual void write16(uint64_t offset, uint16_t value) = 0;
    virtual uint16_t read16(uint64_t offset) = 0;
    virtual void write32(uint64_t offset, uint32_t value) = 0;
    virtual uint32_t read32(uint64_t offset) = 0;
    virtual void write_register(uint64_t offset, const void* data, size_t size) = 0;
    virtual void read_register(uint64_t offset, void* data, size_t size) = 0;
    virtual void write_block(uint64_t offset, const void* data, size_t size) = 0;
    virtual void read_block(uint64_t offset, void* data, size_t size) = 0;

    // Shared higher-level methods that use the virtual methods above.
    virtual void read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering = tlb_data::Strict);

    virtual void write_block_reconfigure(
        const void* mem_ptr,
        tt_xy_pair core,
        uint64_t addr,
        size_t size,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict);

    virtual void noc_multicast_write_reconfigure(
        const void* src,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict);

    // Register reconfigure methods perform 32-bit chunked transfers with strict ordering.
    // Alignment enforcement is the caller's responsibility.
    virtual void read_register_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering = tlb_data::Strict);

    virtual void write_register_reconfigure(
        const void* mem_ptr,
        tt_xy_pair core,
        uint64_t addr,
        size_t size,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict);

    virtual void safe_write16(uint64_t offset, uint16_t value) = 0;

    virtual uint16_t safe_read16(uint64_t offset) = 0;

    virtual void safe_write32(uint64_t offset, uint32_t value);

    virtual uint32_t safe_read32(uint64_t offset);

    virtual void safe_write_register(uint64_t offset, const void* data, size_t size);

    virtual void safe_read_register(uint64_t offset, void* data, size_t size);

    virtual void safe_write_block(uint64_t offset, const void* data, size_t size);

    virtual void safe_read_block(uint64_t offset, void* data, size_t size);

    virtual void safe_write_block_reconfigure(
        const void* mem_ptr,
        tt_xy_pair core,
        uint64_t addr,
        size_t size,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict);

    virtual void safe_read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering = tlb_data::Strict);

    virtual void safe_read_register_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, uint64_t ordering = tlb_data::Strict);

    virtual void safe_write_register_reconfigure(
        const void* mem_ptr,
        tt_xy_pair core,
        uint64_t addr,
        size_t size,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict);

    virtual void safe_noc_multicast_write_reconfigure(
        const void* src,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        NocId noc_id,
        uint64_t ordering = tlb_data::Strict);

    // Installs a per-op MMIO timeout hang check used by the timed memcpy path. No-op by default; only
    // SiliconTlbWindow consults it (simulation windows do not run the timed path). See SiliconTlbWindow.
    virtual void set_io_timeout_hang_check(const std::function<bool(NocId)>& hang_check) {}

    // Shared utility methods.
    TlbHandle& handle_ref() const;
    size_t get_size() const;
    virtual void configure(const tlb_data& new_config);
    uint64_t get_base_address() const;

protected:
    void validate(uint64_t offset, size_t size) const;
    uint64_t get_total_offset(uint64_t offset) const;

    tlb_data make_tlb_config(
        uint64_t addr,
        tt_xy_pair core_end,
        NocId noc_id,
        uint64_t ordering,
        TlbVcDirection direction,
        bool mcast = false,
        tt_xy_pair core_start = {}) const;

    std::unique_ptr<TlbHandle> tlb_handle;
    uint64_t offset_from_aligned_addr = 0;

private:
    template <typename buffer_pointer, typename io_operation>
    void transfer_and_reconfigure(tlb_data config, buffer_pointer buffer, size_t size, io_operation op);
};

}  // namespace tt::umd
