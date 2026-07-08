// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "tt_enums_structs_constants_doxy.hpp"

namespace tt::umd {

/**
 * @defgroup tt_io_window IoWindow
 * @{
 *
 * @brief Host memory-mapped window into device address space.
 *
 * Maps a fixed-size region of host virtual address space to device address space.
 * Through this window, the host can read and write device memory directly.
 *
 * The window has a fixed size determined at construction, but can be reconfigured at
 * runtime to point to different device addresses.
 *
 * Ordering guarantees for all operations are determined by the concrete
 * implementation, which accounts for both host-side factors (e.g., memory
 * caching strategy) and device-side factors (e.g., hardware ordering rules).
 * The APIs below indicate which use cases they are more suited for, but
 * provide minimal guarantees at the interface level.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref TargetIoWindowConfig | Device-side target: core, address, optional NOC, and transaction flags |
 * | @ref HostIoWindowConfig | Host-side properties: caching strategy and requested size |
 * | @ref HostMemoryCaching | Caching strategy (WC or UC) |
 * | @ref WindowFlags | Type-safe transaction attributes (Atomic, Snoop, etc.) |
 *
 */

class IoWindow {
public:
    virtual ~IoWindow() = default;

    /**
     * @brief Writes a block of data through the window. More suited for bulk data transactions.
     * @param offset Byte offset from the window base.
     * @param data Pointer to the source data.
     * @param size Number of bytes to write.
     */
    virtual void write_block(uint64_t offset, const void* data, size_t size) = 0;

    /**
     * @brief Reads a block of data through the window. More suited for bulk data transactions.
     * @param offset Byte offset from the window base.
     * @param data Pointer to the destination buffer.
     * @param size Number of bytes to read.
     */
    virtual void read_block(uint64_t offset, void* data, size_t size) = 0;

    /**
     * @brief Writes a 16-bit value through the window. Suitable for both data and register transactions.
     * @param offset Byte offset from the window base.
     * @param value The 16-bit value to write.
     */
    virtual void write16(uint64_t offset, uint16_t value) = 0;

    /**
     * @brief Reads a 16-bit value through the window. Suitable for both data and register transactions.
     * @param offset Byte offset from the window base.
     * @return uint16_t The value read.
     */
    virtual uint16_t read16(uint64_t offset) = 0;

    /**
     * @brief Writes a 32-bit value through the window. Suitable for both data and register transactions.
     * @param offset Byte offset from the window base.
     * @param value The 32-bit value to write.
     */
    virtual void write32(uint64_t offset, uint32_t value) = 0;

    /**
     * @brief Reads a 32-bit value through the window. Suitable for both data and register transactions.
     * @param offset Byte offset from the window base.
     * @return uint32_t The value read.
     */
    virtual uint32_t read32(uint64_t offset) = 0;

    /**
     * @brief Writes 4-byte aligned data through the window. More suited for register transactions.
     * @param offset Byte offset from the window base. Must be 4-byte aligned.
     * @param data Pointer to the source data.
     * @param size Number of bytes to write. Must be a multiple of 4.
     */
    virtual void write_aligned(uint64_t offset, const void* data, size_t size) = 0;

    /**
     * @brief Reads 4-byte aligned data through the window. More suited for register transactions.
     * @param offset Byte offset from the window base. Must be 4-byte aligned.
     * @param data Pointer to the destination buffer.
     * @param size Number of bytes to read. Must be a multiple of 4.
     */
    virtual void read_aligned(uint64_t offset, void* data, size_t size) = 0;

    /**
     * @brief Reconfigures the window to map to a different region of device address space.
     *
     * Updates the underlying hardware mapping so that subsequent read/write operations
     * target the new device address.
     *
     * @param config Device-side target describing the core, address, and optional NOC.
     * See @ref TargetIoWindowConfig.
     */
    virtual void configure(const TargetIoWindowConfig& config) = 0;

    /**
     * @brief Returns the current device-side target configuration of this window.
     * @return TargetIoWindowConfig The core, address, and optional NOC of the current mapping.
     */
    virtual TargetIoWindowConfig get_target_config() const = 0;

    /**
     * @brief Returns the actual size of this I/O window in bytes.
     *
     * The size is determined by the concrete implementation during construction
     * and may differ from what was requested in @ref HostIoWindowConfig.
     *
     * @return size_t Window size as allocated by the implementation.
     */
    virtual size_t get_size() const = 0;

    /**
     * @brief Returns the host memory caching strategy of this window.
     * @return @ref HostMemoryCaching The caching type (WC or UC), fixed at construction.
     */
    virtual HostMemoryCaching get_memory_caching_type() const = 0;
};

/** @} */  // end of tt_io_window group

}  // namespace tt::umd
