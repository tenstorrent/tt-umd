/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/jtag/jtag_device.hpp"

namespace tt::umd {

class JtagProtocol : public DeviceProtocol {
public:
    JtagProtocol(JtagDevice* jtag_device, uint8_t jlink_id) : jtag_device_(jtag_device), jlink_id_(jlink_id){};

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void wait_for_non_mmio_flush() override;
    bool is_remote() override;

private:
    JtagDevice* jtag_device_;
    uint8_t jlink_id_;
};

}  // namespace tt::umd
