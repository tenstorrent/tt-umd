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

    ~RtlSimTlbHandle() noexcept;

    void configure(const tlb_data& new_config) override;

    SimulationTlbAllocator* get_tlb_allocator() const { return allocator_; }

private:
    RtlSimTlbHandle(SimulationTlbAllocator* allocator, int tlb_id, size_t size, TlbMapping mapping);

    void free_tlb() noexcept override;

    SimulationTlbAllocator* allocator_;
};

}  // namespace tt::umd
