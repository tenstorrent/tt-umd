/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

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

    virtual void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;
    virtual void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) = 0;

    // [[nodiscard]] ensures the caller handles the software fallback
    // if the hardware does not support multicast.
    [[nodiscard]] virtual bool write_to_device_range(
        void* mem_ptr, tt_xy_pair start, tt_xy_pair end, uint64_t addr, uint32_t size) = 0;
};

}  // namespace tt::umd
