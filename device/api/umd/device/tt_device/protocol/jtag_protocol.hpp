/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "mmio_protocol.hpp"
#include "umd/device/jtag/jtag_device.hpp"
#include "umd/device/tt_device/protocol/jtag_interface.hpp"

namespace tt::umd {

class JtagProtocol final : public MmioProtocol, public JtagInterface {
public:
    JtagProtocol(
        std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id, architecture_implementation* architecture_impl);
    virtual ~JtagProtocol() = default;

    /* DeviceProtocol */
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    /* MmioProtocol */
    tt::ARCH get_arch() override;

    int get_communication_device_id() const override;

    IODeviceType get_communication_device_type() override;

    architecture_implementation* get_architecture_implementation() override;

    /* JtagInterface */
    JtagDevice* get_jtag_device() override;

private:
    std::shared_ptr<JtagDevice> jtag_device_;

    int communication_device_id_ = -1;

    architecture_implementation* architecture_impl_ = nullptr;
};

}  // namespace tt::umd
