// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

class SimulationTlbAllocator;
enum TlbMapping : uint8_t;

/**
 * RTL simulation TlbHandle that stores TLB configuration in software only.
 * No hardware register writes are performed since RTL sim has no PCIe BAR0.
 * The stored configuration is used by RtlSimTlbWindow to translate TLB offsets
 * back to tile_read_bytes/tile_write_bytes calls.
 */
class RtlSimTlbHandle : public TlbHandle {
public:
    static std::unique_ptr<RtlSimTlbHandle> create(
        SimulationTlbAllocator* allocator, int tlb_id, size_t size, TlbMapping mapping);

    // TODO: Restore the `free_tlb()` call here once the Metal teardown order is fixed.
    // Currently, Metal's destruction order can destroy the SimulationTlbAllocator before the
    // RtlSimTlbHandles that reference it, so calling free_tlb() from this destructor would
    // dereference an already-destroyed allocator. Defaulting the destructor is a temporary
    // workaround until Metal's shutdown sequence is corrected.
    ~RtlSimTlbHandle() noexcept override = default;

    void configure(const tlb_data& new_config) override;

    SimulationTlbAllocator* get_tlb_allocator() const { return allocator_; }

    tt::ARCH get_arch() const override;

private:
    RtlSimTlbHandle(SimulationTlbAllocator* allocator, int tlb_id, size_t size, TlbMapping mapping);

    void free_tlb() noexcept override;

    SimulationTlbAllocator* allocator_;
};

}  // namespace tt::umd
