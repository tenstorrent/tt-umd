// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Host memory caching strategy for an I/O window.
 */
enum class HostMemoryCaching {
    WC,  ///< Write-Combining — higher write throughput, relaxed ordering.
    UC,  ///< Uncacheable — strict ordering, suitable for register access.
};

/**
 * @brief Describes the device-side target for an I/O window.
 *
 * Specifies which core and address the window maps to, and optionally which
 * NOC to route through. When the mapped address space is not NOC-routed
 * (e.g., direct BAR register space), noc is left as std::nullopt.
 */
struct TargetIoWindowConfig {
    tt_xy_pair core;
    uint64_t addr;
    std::optional<NocId> noc = std::nullopt;
};

/**
 * @brief Describes the host-side properties for an I/O window.
 *
 * Controls the host memory caching strategy and requested window size.
 * A size of 0 instructs the implementation to pick an architecture-appropriate size.
 */
struct HostIoWindowConfig {
    HostMemoryCaching mapping = HostMemoryCaching::WC;
    size_t size = 0;
};

/**
 * @brief Abstract base class representing a host memory-mapped window into device address space.
 *
 * An IoWindow maps a fixed-size region of host virtual address space to a configurable
 * region of device address space. Through this window, the host can read and write device
 * memory using direct pointer operations.
 *
 * The window has a fixed size determined at construction, but can be reconfigured at
 * runtime to point to different device addresses.
 *
 * Concrete implementations:
 * - TlbWindow: silicon/PCIe devices — owns the BAR mapping and hardware TLB entry directly.
 * - TTSimIoWindow: TTSim simulation backend.
 * - RtlSimIoWindow: RTL simulation backend.
 */
class IoWindow {
public:
    virtual ~IoWindow() = default;

    // -- Data access (unordered) ----------------------------------------------

    /**
     * @brief Writes a block of data through the window with no memory ordering guarantees.
     * @param offset Byte offset from the window base.
     * @param data Pointer to the source data.
     * @param size Number of bytes to write.
     */
    virtual void write_block(uint64_t offset, const void* data, size_t size) = 0;

    /**
     * @brief Reads a block of data through the window with no memory ordering guarantees.
     * @param offset Byte offset from the window base.
     * @param data Pointer to the destination buffer.
     * @param size Number of bytes to read.
     */
    virtual void read_block(uint64_t offset, void* data, size_t size) = 0;

    // -- Register access (ordered) --------------------------------------------

    /**
     * @brief Writes a 16-bit value with strict memory ordering guarantees.
     * @param offset Byte offset from the window base.
     * @param value The 16-bit value to write.
     */
    virtual void write16(uint64_t offset, uint16_t value) = 0;

    /**
     * @brief Reads a 16-bit value with strict memory ordering guarantees.
     * @param offset Byte offset from the window base.
     * @return uint16_t The value read.
     */
    virtual uint16_t read16(uint64_t offset) = 0;

    /**
     * @brief Writes a 32-bit value with strict memory ordering guarantees.
     * @param offset Byte offset from the window base.
     * @param value The 32-bit value to write.
     */
    virtual void write32(uint64_t offset, uint32_t value) = 0;

    /**
     * @brief Reads a 32-bit value with strict memory ordering guarantees.
     * @param offset Byte offset from the window base.
     * @return uint32_t The value read.
     */
    virtual uint32_t read32(uint64_t offset) = 0;

    /**
     * @brief Writes to a device register with strict memory ordering guarantees.
     * @param offset Byte offset from the window base. Must be 4-byte aligned.
     * @param data Pointer to the source data.
     * @param size Number of bytes to write. Must be a multiple of 4.
     */
    virtual void write_register(uint64_t offset, const void* data, size_t size) = 0;

    /**
     * @brief Reads from a device register with strict memory ordering guarantees.
     * @param offset Byte offset from the window base. Must be 4-byte aligned.
     * @param data Pointer to the destination buffer.
     * @param size Number of bytes to read. Must be a multiple of 4.
     */
    virtual void read_register(uint64_t offset, void* data, size_t size) = 0;

    // -- Configuration --------------------------------------------------------

    /**
     * @brief Reconfigures the window to map to a different region of device address space.
     *
     * Updates the underlying hardware mapping so that subsequent read/write operations
     * target the new device address.
     *
     * @param config Device-side target describing the core, address, and optional NOC.
     */
    virtual void configure(const TargetIoWindowConfig& config) = 0;

    // -- Properties -----------------------------------------------------------

    /**
     * @brief Returns the current device-side target configuration of this window.
     * @return TargetIoWindowConfig The core, address, and optional NOC of the current mapping.
     */
    virtual TargetIoWindowConfig get_target_config() const = 0;

    /**
     * @brief Returns the actual size of this I/O window in bytes.
     *
     * The size is determined by the concrete implementation during construction
     * and may differ from what was requested in HostIoWindowConfig.
     *
     * @return size_t Window size as allocated by the implementation.
     */
    virtual size_t get_size() const = 0;

    /**
     * @brief Returns the host memory caching strategy of this window.
     * @return HostMemoryCaching The caching type (WC or UC), fixed at construction.
     */
    virtual HostMemoryCaching get_memory_caching_type() const = 0;

protected:
    IoWindow() = default;
};

}  // namespace tt::umd
