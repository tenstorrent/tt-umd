/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

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

    // Strict-ordered MMIO register read. Transports with a dedicated register aperture (PCIe) override
    // this; the default falls back to a (relaxed) block read, which is the correct behaviour for transports
    // that route registers as ordinary memory (e.g. remote/Ethernet).
    virtual void read_from_device_reg(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
        validate_register_access(addr, size);
        read_from_device(mem_ptr, core, addr, size, noc_id);
    }

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
};

}  // namespace tt::umd
