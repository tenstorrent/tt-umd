/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class PCIDevice;

/**
 * PcieInterface defines PCIe-specific operations beyond the basic DeviceProtocol.
 *
 * This includes DMA transfers, BAR register access, NOC multicast writes, and
 * direct access to the underlying PCIDevice.
 */
class PcieInterface {
public:
    virtual ~PcieInterface() = default;

    virtual PCIDevice* get_pci_device() = 0;

    // Return true on success, false if DMA is unavailable.
    [[nodiscard]] virtual bool dma_write_to_device(
        const void* src, size_t size, tt_xy_pair core, uint64_t addr, NocId noc_id) = 0;
    // Return true on success, false if DMA is unavailable.
    [[nodiscard]] virtual bool dma_read_from_device(
        void* dst, size_t size, tt_xy_pair core, uint64_t addr, NocId noc_id) = 0;
    // Return true on success, false if DMA is unavailable.
    [[nodiscard]] virtual bool dma_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) = 0;

    virtual void dma_d2h(void* dst, uint32_t src, size_t size) = 0;
    virtual void dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) = 0;
    virtual void dma_h2d(uint32_t dst, const void* src, size_t size) = 0;
    virtual void dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) = 0;

    virtual void noc_multicast_write(
        const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) = 0;

    virtual void bar_write32(uint32_t addr, uint32_t data) = 0;
    virtual uint32_t bar_read32(uint32_t addr) = 0;

    // Hook for the timed MMIO path: invoked (with the in-flight op's NOC) when a single op exceeds its
    // per-op budget. Returns true if that NOC is confirmed hung (abort the transfer with
    // DeviceTimeoutError), false to treat the slow op as a false positive and continue.
    virtual void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) = 0;
};

}  // namespace tt::umd
