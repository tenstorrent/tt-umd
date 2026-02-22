/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

class RemoteProtocol final : public DeviceProtocol, public RemoteInterface {
public:
    explicit RemoteProtocol(
        std::unique_ptr<RemoteCommunication> remote_communication, architecture_implementation* architecture_impl);

    /* DeviceProtocol */
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    tt::ARCH get_arch() override;

    int get_communication_device_id() const override;

    IODeviceType get_communication_device_type() override;

    architecture_implementation* get_architecture_implementation() override;

    void detect_hang_read(uint32_t data_read = HANG_READ_VALUE) override;

    bool is_hardware_hung() override;

    /* RemoteInterface */
    RemoteCommunication* get_remote_communication() override;

    void wait_for_non_mmio_flush() override;

private:
    std::unique_ptr<RemoteCommunication> remote_communication_ = nullptr;

    architecture_implementation* architecture_impl_ = nullptr;
};

}  // namespace tt::umd
