// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/types/io_window_config.hpp"

namespace tt::umd {

/**
 * An IoWindow over the kernel driver's scalar accesses.
 *
 * Nothing is mapped. On an architecture that offers a translation window, a window is a region of
 * host address space and an access through it is a load or a store; here the driver owns the only
 * inbound apertures, so an access is a round trip through it at the window's base plus the offset.
 * The interface is the same so that callers above need not know which of the two they hold, but
 * the cost is not: a block transfer is one round trip per word.
 *
 * The address is flat and names its own target, so a window carries no coordinate: only the origin
 * is accepted, and a multicast grid cannot be represented at all. A scalar access has nowhere to
 * carry transaction attributes either, so Atomic and Snoop are refused; the direction field is
 * advice this window has nothing to gain from, and is ignored as WindowFlags permits.
 *
 * Its base is whatever it was last configured with. Nothing is allocated to bound it, so the size
 * is the span the window accepts offsets within: whatever the caller asked for, or, when the caller
 * asked for nothing and left the choice here, the rest of the address space from the base. A mapped
 * window cannot offer the latter.
 */
class KmdNocWindow : public IoWindow {
public:
    /**
     * @param pci_device The device every transfer through this window is issued on. Borrowed: the
     * protocol on the same device owns it and outlives the windows served from it.
     * @param config The target this window starts out pointing at.
     * @param size The span to accept offsets within, or 0 for unbounded.
     */
    KmdNocWindow(PCIDevice* pci_device, const TargetIoWindowConfig& config, size_t size = 0);
    ~KmdNocWindow() override;

    void write_block(uint64_t offset, const void* data, size_t size) override;
    void read_block(uint64_t offset, void* data, size_t size) override;

    void write16(uint64_t offset, uint16_t value) override;
    uint16_t read16(uint64_t offset) override;

    void write32(uint64_t offset, uint32_t value) override;
    uint32_t read32(uint64_t offset) override;

    void write64(uint64_t offset, uint64_t value) override;
    uint64_t read64(uint64_t offset) override;

    void write_aligned(uint64_t offset, const void* data, size_t size) override;
    void read_aligned(uint64_t offset, void* data, size_t size) override;

    /** @throws error::RuntimeError for a target or an attribute this path cannot represent. */
    void configure(const TargetIoWindowConfig& config) override;
    void configure(const TargetIoWindowConfig& config, IoOrdering ordering) override;

    TargetIoWindowConfig get_target_config() const override;
    IoOrdering get_io_ordering() const override;
    size_t get_size() const override;
    HostMemoryCaching get_memory_caching_type() const override;

private:
    /** Refuses an access that is misaligned, or that runs past a window with a size. */
    void validate(uint64_t offset, size_t size, uint32_t width) const;

    PCIDevice* pci_device_;
    TargetIoWindowConfig config_;

    // Resolved when the target is accepted rather than per access, so that a target this path
    // cannot serve is refused instead of being quietly served from the wrong address space.
    NocAddressSpace space_;

    size_t size_;
};

}  // namespace tt::umd
