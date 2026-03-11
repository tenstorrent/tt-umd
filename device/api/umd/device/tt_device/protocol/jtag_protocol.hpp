/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"

namespace tt::umd {

class JtagDevice;

/**
 * JtagProtocol implements DeviceProtocol and JtagInterface for JTAG-connected devices.
 *
 * Method implementations will be migrated from TTDevice in subsequent PRs.
 */
class JtagProtocol : public DeviceProtocol, public JtagInterface {
public:
    JtagProtocol(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id);

    ~JtagProtocol() override = default;

    // DeviceProtocol interface.
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    bool write_to_device_range(
        const void* mem_ptr, tt_xy_pair start, tt_xy_pair end, uint64_t addr, uint32_t size) override;

    // JtagInterface.
    JtagDevice* get_jtag_device() override;

private:
    std::shared_ptr<JtagDevice> jtag_device_;
};

}  // namespace tt::umd
