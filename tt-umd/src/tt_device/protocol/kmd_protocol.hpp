// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "tt-umd/pcie/pci_device.hpp"
#include "tt-umd/tt_device/protocol/device_protocol.hpp"
#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/register_access_width.hpp"
#include "tt-umd/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Device I/O carried by the kernel driver, one naturally aligned access at a time.
 *
 * PcieProtocol maps a translation window and dereferences it. An architecture that exposes no
 * window to userspace -- Quasar is the one that does so today -- instead has the driver keep the
 * only usable inbound apertures and perform each access itself, so a transfer here is decomposed
 * into single accesses rather than built out of loads and stores.
 *
 * The address is flat and carries its own target, so the coordinate the interface takes is not a
 * destination here and only the origin is accepted. The NOC selects the address space instead:
 * the default one reaches ordinary targets, and the system NOC reaches the device-local space
 * where the translation and firewall configuration blocks live.
 */
class KmdProtocol : public DeviceProtocol {
public:
    /**
     * An access is a driver round trip, so a large transfer is a correspondingly large amount of
     * time rather than a large amount of bandwidth. Refusing past this point keeps a caller that
     * meant to use a bulk path from waiting on hundreds of thousands of them instead.
     */
    static constexpr size_t MAX_TRANSFER_SIZE = 4096;

    KmdProtocol(std::unique_ptr<PCIDevice> pci_device, int mmio_id);
    ~KmdProtocol() override;

    void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(
        void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;
    void write_ctrl(
        const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) override;

    /** Always false: there is no hardware multicast on this path, so the caller must unicast. */
    [[nodiscard]] bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) override;

    int get_mmio_id() override;

    /** Borrowed, not transferred: this protocol owns the device for as long as it lives. */
    PCIDevice* get_pci_device();

private:
    /** The address space @p noc_id names, rejecting a NOC this path cannot serve. */
    static NocAddressSpace address_space(NocId noc_id);

    /** Rejects a request the driver would refuse, naming what is wrong with it. */
    static void validate(tt_xy_pair core, uint64_t addr, size_t size, uint32_t access_width);

    std::unique_ptr<PCIDevice> pci_device_;
    int mmio_id_;
};

}  // namespace tt::umd
