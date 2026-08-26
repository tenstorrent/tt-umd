// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "umd/device/io_window/io_window.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/io_window_config.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Base class for TlbWindow implementations that contains all shared logic.
 * The memory access methods are pure virtual to allow different implementations
 * for silicon (direct memory access) vs simulation (communicator-based access).
 */
class TlbWindow : public IoWindow {
public:
    TlbWindow(std::unique_ptr<TlbHandle> handle, const tlb_data config = {});

    virtual ~TlbWindow() = default;

    // Pure virtual methods for memory access - to be implemented by derived classes.
    void write16(uint64_t offset, uint16_t value) override = 0;
    uint16_t read16(uint64_t offset) override = 0;
    void write32(uint64_t offset, uint32_t value) override = 0;
    uint32_t read32(uint64_t offset) override = 0;
    virtual void write_register(uint64_t offset, const void* data, size_t size) = 0;
    virtual void read_register(uint64_t offset, void* data, size_t size) = 0;
    void write_block(uint64_t offset, const void* data, size_t size) override = 0;
    void read_block(uint64_t offset, void* data, size_t size) override = 0;

    // IoWindow spec surface.
    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;
    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;
    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    HostMemoryCaching get_memory_caching_type() const override;

    // Installs a per-op MMIO timeout hang check used by the timed memcpy path. No-op by default; only
    // SiliconTlbWindow consults it (simulation windows do not run the timed path). See SiliconTlbWindow.
    virtual void set_io_timeout_hang_check(const std::function<bool(NocId)>& hang_check) {}

    // Installs safe-I/O as window-level policy: once enabled, the memory-access methods above recover
    // from SIGBUS by throwing SigbusError instead of crashing the process. No-op by default; only
    // SiliconTlbWindow can actually offer this (simulation windows never touch mapped device memory).
    // Callers needing chunked transfers use the free functions in io_window_reconfigure.hpp, which honor
    // whatever policy is installed here since they operate through this window's ops.
    virtual void set_safe_io(bool enable) {}

    // Shared utility methods.
    TlbHandle& handle_ref() const;
    size_t get_size() const override;
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
        WindowFlags flags,
        bool mcast = false,
        tt_xy_pair core_start = {}) const;

    std::unique_ptr<TlbHandle> tlb_handle;
    uint64_t offset_from_aligned_addr = 0;
};

}  // namespace tt::umd
