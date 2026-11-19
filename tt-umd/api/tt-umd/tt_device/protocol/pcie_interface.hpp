/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <optional>

#include "tt-umd/types/noc_id.hpp"
#include "tt-umd/types/power_state.hpp"

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
     * @brief Returns the number of lanes the PCIe link trained to.
     * @return std::optional<uint32_t> Lane count, or std::nullopt if it can't be determined.
     */
    virtual std::optional<uint32_t> get_lane_count() const = 0;

    /**
     * @brief Returns the physical PCIe slot the device sits in.
     * @return std::optional<int> Slot number, or std::nullopt if the platform does not report one.
     */
    virtual std::optional<int> get_physical_slot() const = 0;

    /**
     * @brief Requests a hardware power domain state change through the kernel driver.
     *
     * A no-op on architectures or KMD versions without power state management.
     *
     * @param state The requested power state.
     */
    virtual void set_power_state(PowerState state) = 0;

    /**
     * @brief Requests hardware power domain changes through the kernel driver.
     *
     * Only the valid flags are applied; the others keep their current state. A no-op where the driver or
     * architecture has no power state management, which is the default.
     *
     * @param power_flags_bitfield Requested state of each power flag, one bit per flag.
     * @param power_flags_valid How many of the low flags in power_flags_bitfield are valid, in bits 0-3.
     */
    virtual void set_power_state(
        [[maybe_unused]] uint16_t power_flags_bitfield, [[maybe_unused]] uint8_t power_flags_valid) {}
};

}  // namespace tt::umd
