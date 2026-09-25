// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <memory>

struct tt_device_t;

namespace tt::umd {

class PCIDevice;

/**
 * One naturally aligned access, performed by the kernel driver on the caller's behalf.
 *
 * Wormhole and Blackhole let userspace map a translation window and dereference it, so their
 * transport needs nothing like this. Quasar does: the driver keeps the only usable inbound
 * apertures, programs one per access and performs the access itself, so every transfer above is
 * built out of these rather than out of loads and stores.
 *
 * The address is flat. Whatever selects the target -- a folded-in coordinate, or a chiplet-local
 * address -- is already part of it by the time it arrives here.
 */
class KmdScalarNocAccess {
public:
    /** Access width in bytes. The driver serves these four and rejects anything else. */
    static constexpr uint32_t MAX_ACCESS_WIDTH = 8;

    /** Reinterprets the address as chiplet-local, reached through the second inbound aperture. */
    static constexpr uint32_t FLAG_LOCAL_ADDRESS = 1u << 0;

    /**
     * @param pci_device The device whose driver handle the accesses are issued on. Held here
     * rather than borrowed: closing the device frees the handle, and nothing else outlives these
     * accesses to keep it open.
     */
    explicit KmdScalarNocAccess(std::unique_ptr<PCIDevice> pci_device);
    ~KmdScalarNocAccess();

    KmdScalarNocAccess(const KmdScalarNocAccess&) = delete;
    KmdScalarNocAccess& operator=(const KmdScalarNocAccess&) = delete;

    /**
     * @param addr Flat address, naturally aligned to @p width.
     * @param value Receives the value, zero-extended to 64 bits.
     * @param width Access width in bytes; 1, 2, 4 or 8.
     * @param flags Zero or FLAG_LOCAL_ADDRESS.
     * @throws error::RuntimeError if the driver refuses the access.
     */
    void read(uint64_t addr, uint64_t* value, uint32_t width, uint32_t flags);

    /**
     * @param addr Flat address, naturally aligned to @p width.
     * @param value Value to write; only the low @p width bytes are used.
     * @param width Access width in bytes; 1, 2, 4 or 8.
     * @param flags Zero or FLAG_LOCAL_ADDRESS.
     * @throws error::RuntimeError if the driver refuses the access.
     */
    void write(uint64_t addr, uint64_t value, uint32_t width, uint32_t flags);

private:
    std::unique_ptr<PCIDevice> pci_device_;
    tt_device_t* handle_;
};

}  // namespace tt::umd
