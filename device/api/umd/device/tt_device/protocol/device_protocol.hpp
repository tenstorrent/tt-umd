/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/communication_protocol.hpp"
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
        const void* mem_ptr, tt_xy_pair start, tt_xy_pair end, uint64_t addr, uint32_t size) = 0;

    virtual tt::ARCH get_arch() = 0;
    virtual architecture_implementation* get_architecture_implementation() = 0;

    virtual int get_communication_device_id() const = 0;
    virtual IODeviceType get_communication_device_type() = 0;

    virtual void detect_hang_read(uint32_t data_read = HANG_READ_VALUE) = 0;
    virtual bool is_hardware_hung() = 0;
};

}  // namespace tt::umd
