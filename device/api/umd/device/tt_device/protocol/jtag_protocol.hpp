/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class JtagDevice;

/**
 * JtagProtocol implements DeviceProtocol and JtagInterface for JTAG-connected devices.
 */
class JtagProtocol : public DeviceProtocol, public JtagInterface {
public:
    JtagProtocol(std::unique_ptr<JtagDevice> jtag_device, uint8_t jlink_id);

    ~JtagProtocol() override;

    // DeviceProtocol interface.
    void write_data(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_data(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void write_ctrl(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_ctrl(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    bool write_to_core_range(
        const void* mem_ptr, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id)
        override;
    int get_mmio_id() override;

    // JtagInterface.
    JtagDevice* get_jtag_device() override;

private:
    std::unique_ptr<JtagDevice> jtag_device_;
    int mmio_id_;
};

}  // namespace tt::umd
