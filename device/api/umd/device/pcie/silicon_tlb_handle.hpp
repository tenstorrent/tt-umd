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
    SiliconTlbHandle(tt_device_t* tt_device, size_t size, const TlbMapping tlb_mapping = TlbMapping::UC);

    ~SiliconTlbHandle() noexcept override;

    /**
     * Configures the TLB with the provided configuration.
     *
     * @param new_config The new configuration for the TLB.
     */
    void configure(const tlb_data& new_config) override;

    /**
     * Returns the base mapped address of the TLB.
     */
    uint8_t* get_base() override;

    /**
     * Returns the size of the TLB.
     */
    size_t get_size() const override;

    /**
     * Returns the current configuration of the TLB.
     */
    const tlb_data& get_config() const override;

    /**
     * Returns the TLB mapping type (UC or WC).
     */
    TlbMapping get_tlb_mapping() const override;

    /**
     * Returns the TLB ID, actually representing index of TLB in BAR0.
     */
    int get_tlb_id() const override;

private:
    void free_tlb() noexcept override;

    int tlb_id;
    uint8_t* tlb_base;
    size_t tlb_size;
    tlb_data tlb_config;
    tt_device_t* tt_device_;
    TlbMapping tlb_mapping;
    tt_tlb_t* tlb_handle_ = nullptr;
};

}  // namespace tt::umd
