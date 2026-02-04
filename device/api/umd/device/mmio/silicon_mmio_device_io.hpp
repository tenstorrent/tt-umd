// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/mmio/mmio_device_io.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/pcie/tlb_handle.hpp"
#include "umd/device/tt_kmd_lib/tt_kmd_lib.h"

namespace tt::umd {

/**
 * Silicon implementation of MMIODeviceIO that uses TLB allocation
 * and delegates to TlbWindow for actual operations.
 */
class SiliconMMIODeviceIO : public MMIODeviceIO {
public:
    /**
     * Constructor that creates a TLB allocation and TlbWindow.
     *
     * @param tt_device Pointer to the tt_device structure representing the PCI device.
     * @param size Size of the TLB to allocate.
     * @param tlb_mapping Type of TLB mapping (UC or WC).
     * @param config Initial TLB configuration.
     */
    SiliconMMIODeviceIO(
        tt_device_t* tt_device,
        size_t size,
        const TlbMapping tlb_mapping = TlbMapping::UC,
        const tlb_data& config = {});

    ~SiliconMMIODeviceIO() override = default;

    void write32(uint64_t offset, uint32_t value) override;

    uint32_t read32(uint64_t offset) override;

    void write_register(uint64_t offset, const void* data, size_t size) override;

    void read_register(uint64_t offset, void* data, size_t size) override;

    void write_block(uint64_t offset, const void* data, size_t size) override;

    void read_block(uint64_t offset, void* data, size_t size) override;

    void read_block_reconfigure(
        void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) override;

    void write_block_reconfigure(
        const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering = tlb_data::Strict) override;

    void noc_multicast_write_reconfigure(
        void* dst,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint64_t ordering = tlb_data::Strict) override;

    size_t get_size() const override;

    void configure(const tlb_data& new_config) override;

    uint64_t get_base_address() const override;

    /**
     * Get access to the underlying TlbWindow for advanced operations.
     */
    TlbWindow* get_tlb_window() const;

protected:
    void validate(uint64_t offset, size_t size) const override;

private:
    std::unique_ptr<TlbWindow> tlb_window_;
};

}  // namespace tt::umd