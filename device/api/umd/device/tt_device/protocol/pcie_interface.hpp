/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <functional>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief PCIe-specific device access: BAR register I/O and NUMA topology.
 *
 * Exposes operations that are only meaningful for PCIe-connected devices.
 * Available from TTDevice::get_pcie_interface() when the active transport is PCIe.
 */
class PcieInterface {
public:
    virtual ~PcieInterface() = default;

    /**
     * @brief Writes a 32-bit value to a BAR-relative device address.
     * @param addr BAR-relative address.
     * @param data The 32-bit value to write.
     */
    virtual void bar_write32(uint32_t addr, uint32_t data) = 0;

    /**
     * @brief Reads a 32-bit value from a BAR-relative device address.
     * @param addr BAR-relative address.
     * @return uint32_t The value read.
     */
    virtual uint32_t bar_read32(uint32_t addr) = 0;

    /**
     * @brief Returns the NUMA node associated with this PCIe device.
     * @return int NUMA node ID, or -1 if the system is non-NUMA.
     */
    virtual int get_numa_node() const = 0;

    /**
     * @brief Exports (core, addr) as a dma-buf fd for peer-to-peer PCIe DMA, backed by a dedicated
     * TLB window.
     *
     * Temporary: this is not part of the Base API spec. It lives here only until the spec covers
     * dma-buf export; move it at that point.
     *
     * @param core Core to target.
     * @param addr Address within the core to aim the exported region at; must be page-aligned.
     * @param size Number of bytes to export; must be page-aligned and non-zero.
     * @param ordering Ordering mode for the TLB window backing the export.
     * @param noc_id NOC to route the exported traffic over.
     * @return int The dma-buf fd; the caller owns it and must close() it.
     */
    virtual int export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) = 0;

    /**
     * @brief Registers the callback consulted on an IO-op timeout to distinguish a hung NOC from a
     * slow one.
     *
     * Temporary: this is an implementation detail rather than
     * something the Base API spec should own. It lives here only until we align api
     * with TTDeviceModel; move/remove it at that point.
     *
     * @param hang_check Callback invoked with the NOC id of the in-flight op; an empty callback
     * disables hang detection.
     */
    virtual void set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) = 0;
};

}  // namespace tt::umd
