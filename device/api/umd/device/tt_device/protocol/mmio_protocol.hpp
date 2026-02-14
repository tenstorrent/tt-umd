/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

class MmioProtocol : public DeviceProtocol {
public:
    virtual tt::ARCH get_arch() = 0;

    virtual int get_communication_device_id() const = 0;

    virtual architecture_implementation* get_architecture_implementation() = 0;

    virtual void detect_hang_read(uint32_t data_read = HANG_READ_VALUE) = 0;

    virtual bool is_hardware_hung() = 0;
};

}  // namespace tt::umd
