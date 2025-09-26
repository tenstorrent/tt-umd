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
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void wait_for_non_mmio_flush() override;
    bool is_remote() override;
};

}  // namespace tt::umd
