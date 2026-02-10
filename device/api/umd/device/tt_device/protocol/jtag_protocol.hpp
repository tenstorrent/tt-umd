/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/jtag/jtag_device.hpp"

namespace tt::umd {

class JtagProtocol : public DeviceProtocol {
public:
    JtagProtocol(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id);
    virtual ~JtagProtocol() = default;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

private:
    std::shared_ptr<JtagDevice> jtag_device_;

    int communication_device_id_ = -1;
};

}  // namespace tt::umd
