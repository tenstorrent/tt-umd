// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Base interface for all device communication protocols.
 *
 * Each concrete protocol (PCIe, JTAG, Remote/Ethernet) implements this interface,
 * providing a uniform way to perform basic device I/O regardless of the underlying
 * transport.
 *
 * The interface distinguishes between data and control paths. Data operations
 * are optimized for throughput. Control operations are optimized for ordered
 * transactions, where visibility and completion ordering matter.
 */
class DeviceProtocol {
public:
    virtual ~DeviceProtocol() = default;

    /**
     * @brief Writes a block of host data to a device core, suited for bulk data transfers.
     *
     * @param src Pointer to the source host memory.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to write.
     * @param noc_id NOC to route through.
     */
    virtual void write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;

    /**
     * @brief Reads a block of data from a device core into a host buffer, suited for bulk data transfers.
     *
     * @param dst Pointer to the destination host buffer.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to read.
     * @param noc_id NOC to route through.
     */
    virtual void read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;

    /**
     * @brief Writes host data to a device core, suited for register and control transactions.
     *
     * @param src Pointer to the source host memory.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to write.
     * @param noc_id NOC to route through.
     */
    virtual void write_ctrl(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;

    /**
     * @brief Reads data from a device core into a host buffer, suited for register and control transactions.
     *
     * @param dst Pointer to the destination host buffer.
     * @param core Target core coordinates.
     * @param addr Device address on the target core.
     * @param size Number of bytes to read.
     * @param noc_id NOC to route through.
     */
    virtual void read_ctrl(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;

    /**
     * @brief Writes data to a range of cores via hardware multicast, if supported.
     *
     * Marked [[nodiscard]] — not all protocols support hardware multicast, so the
     * caller must check the return value to handle unsupported cases.
     *
     * @param src Pointer to the source host memory.
     * @param core_start First core in the multicast rectangle.
     * @param core_end Last core in the multicast rectangle.
     * @param addr Device address on each target core.
     * @param size Number of bytes to write.
     * @param noc_id NOC to route through.
     * @return true if the hardware performed the multicast; false if the caller
     *         must fall back to software unicast.
     */
    [[nodiscard]] virtual bool write_to_core_range(
        const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint32_t size, NocId noc_id) = 0;

    /**
     * @brief Returns the MMIO device ID, identifying both the device and its protocol instance.
     * @return int The MMIO device identifier.
     */
    virtual int get_mmio_id() = 0;
};

}  // namespace tt::umd
