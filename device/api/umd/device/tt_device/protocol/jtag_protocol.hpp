/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"

namespace tt::umd {

class JtagProtocol final : public DeviceProtocol, public JtagInterface {
public:
    JtagProtocol(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id);
    virtual ~JtagProtocol() = default;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    JtagDevice* get_jtag_device() override;

private:
    std::shared_ptr<JtagDevice> jtag_device_;

    int communication_device_id_ = -1;
};

}  // namespace tt::umd
