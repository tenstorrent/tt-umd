/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"

namespace tt::umd {

class RemoteCommunication;

/**
 * RemoteProtocol implements DeviceProtocol and RemoteInterface for remote/Ethernet-connected devices.
 *
 * Wraps a RemoteCommunication object and delegates I/O operations through it.
 */
class RemoteProtocol : public DeviceProtocol, public RemoteInterface {
public:
    explicit RemoteProtocol(std::unique_ptr<RemoteCommunication> remote_communication);

    ~RemoteProtocol() override = default;

    // DeviceProtocol interface.
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    bool write_to_device_range(
        const void* mem_ptr, tt_xy_pair start, tt_xy_pair end, uint64_t addr, uint32_t size) override;

    // RemoteInterface.
    RemoteCommunication* get_remote_communication() override;
    void wait_for_non_mmio_flush() override;

private:
    std::unique_ptr<RemoteCommunication> remote_communication_;
};

}  // namespace tt::umd
