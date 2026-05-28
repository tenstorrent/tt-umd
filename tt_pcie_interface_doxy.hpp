// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace tt::umd {

/**
 * @defgroup tt_pcie_interface PcieInterface
 * @{
 *
 * @brief PCIe-specific device access: BAR register I/O and NUMA topology.
 *
 * Exposes operations that are only meaningful for PCIe-connected devices.
 * Available from @ref TTDeviceModel when the active transport is PCIe.
 *
 */

/**
 * @brief PCIe-specific device operations.
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
};

/** @} */  // end of tt_pcie_interface group

}  // namespace tt::umd
