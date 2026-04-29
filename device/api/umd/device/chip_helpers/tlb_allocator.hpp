// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <memory>

#include "umd/device/types/tlb.hpp"

namespace tt::umd {

class TlbHandle;

/**
 * Backend-agnostic interface for TLB allocation.
 *
 * Concrete implementations:
 * - KmdTlbAllocator: silicon, delegates to KMD via PCIDevice::allocate_tlb.
 * - SimulationTlbAllocator: simulation, in-process bitmap allocator.
 */
class TlbAllocator {
public:
    virtual ~TlbAllocator() = default;

    /**
     * Allocate a TlbHandle of the requested size.
     *
     * @param size Requested TLB size in bytes. If 0, the allocator is free to
     *             pick any architecturally supported size (typically smallest first).
     * @param mapping UC or WC.
     * @return Owning TlbHandle. Throws on failure.
     */
    virtual std::unique_ptr<TlbHandle> allocate(size_t size, TlbMapping mapping) = 0;
};

}  // namespace tt::umd
