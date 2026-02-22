// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <set>
#include <unordered_set>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

class SysmemManager;

class RemoteCommunicationLegacyFirmware : public RemoteCommunication {
public:
    RemoteCommunicationLegacyFirmware(
        DeviceProtocol* device_protocol, EthCoord target_chip, SysmemManager* sysmem_manager = nullptr);

    void read_non_mmio(
        tt_xy_pair target_core,
        void* dest,
        uint64_t core_src,
        uint32_t size_in_bytes,
        const std::chrono::milliseconds timeout_ms = timeout::NON_MMIO_RW_TIMEOUT) override;

    void write_to_non_mmio(
        tt_xy_pair target_core,
        const void* src,
        uint64_t core_dest,
        uint32_t size_in_bytes,
        bool broadcast = false,
        std::vector<int> broadcast_header = {},
        const std::chrono::milliseconds timeout_ms = timeout::NON_MMIO_RW_TIMEOUT) override;

    void wait_for_non_mmio_flush(const std::chrono::milliseconds timeout_ms = timeout::NON_MMIO_RW_TIMEOUT) override;

private:
    EthCoord target_chip;
};

}  // namespace tt::umd
