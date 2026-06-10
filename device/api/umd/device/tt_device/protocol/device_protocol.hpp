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
 * DeviceProtocol is the base interface for all device communication protocols.
 *
 * Each concrete protocol (PCIe, JTAG, Remote/Ethernet) implements this interface,
 * providing a uniform way to perform basic device I/O regardless of the underlying
 * transport.
 *
 */
class DeviceProtocol {
public:
    virtual ~DeviceProtocol() = default;

    virtual void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;
    virtual void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) = 0;

    // [[nodiscard]] tells the compiler that the return value should not be ignored.
    // This ensures the caller handles the software fallback
    // if the hardware does not support multicast.
    // @return true if the hardware multicast was performed, false if the caller must do a software unicast fallback.
    [[nodiscard]] virtual bool write_to_core_range(
        const void* mem_ptr,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        uint32_t size,
        NocId noc_id) = 0;

    // Returns the MMIO device ID, used to uniquely identify both the device and its associated protocol instance.
    virtual int get_mmio_id() = 0;

    // Optional hook for the timed MMIO path: invoked (with the in-flight op's NOC) when a single op
    // exceeds its per-op budget. Returns true if that NOC is confirmed hung (abort the transfer with
    // DeviceTimeoutError), false to treat the slow op as a false positive and continue. Default is a
    // no-op for protocols without a timed MMIO path.
    virtual void set_io_timeout_callback(const std::function<bool(NocId)>& /*hang_check*/) {}
};

}  // namespace tt::umd
