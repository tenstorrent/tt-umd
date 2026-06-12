/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "umd/device/tt_device/ethernet_broadcast.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/remote_interface.hpp"
#include "umd/device/types/xy_pair.hpp"

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
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) override;
    bool write_to_core_range(
        const void* mem_ptr, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint32_t size, NocId noc_id)
        override;
    int get_mmio_id() override;

    // RemoteInterface.
    RemoteCommunication* get_remote_communication() override;
    void wait_for_non_mmio_flush() override;

private:
    std::unique_ptr<RemoteCommunication> remote_communication_;
    std::unique_ptr<EthernetBroadcast> ethernet_broadcast_;
};

}  // namespace tt::umd
