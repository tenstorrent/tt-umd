// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/tt_kmd_lib/tt_kmd_lib.h"
#include "umd/device/types/tlb.hpp"

namespace tt::umd {

// Forward declaration.
class PCIDevice;

/**
 * Hardware TLB Handle implementation that manages actual silicon TLBs.
 * This class handles allocation, mapping, and configuration of hardware TLBs
 * through the kernel mode driver (KMD).
 */
class SiliconTlbHandle : public TlbHandle {
public:
    /**
     * Constructor for SiliconTlbHandle.
     * Allocates a TLB from KMD of the specified size and maps it to the user space.
     *
     * @param tt_device Pointer to the tt_device structure representing the PCI device.
     * @param size Size of the TLB to allocate.
     * @param tlb_mapping Type of TLB mapping (UC or WC). The first mapping of TLB determines its caching behavior.
     */
    SiliconTlbHandle(PCIDevice& pci_device, size_t size, const TlbMapping tlb_mapping = TlbMapping::UC);

    ~SiliconTlbHandle() noexcept override;

    void configure(const tlb_data& new_config) override;

    tt::ARCH get_arch() const override;

    int export_dmabuf(uint64_t offset = 0, uint64_t size = 0) const override;

private:
    void free_tlb() noexcept override;

    PCIDevice& pci_device_;
    tt_tlb_t* tlb_handle_ = nullptr;
};

}  // namespace tt::umd
