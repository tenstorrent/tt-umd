// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <set>
#include <unordered_set>

#include "umd/device/lite_fabric/lite_fabric.hpp"
#include "umd/device/tt_device/remote_communication.hpp"

namespace tt::umd {

class SysmemManager;

class RemoteCommunicationLiteFabric : public RemoteCommunication {
public:
    RemoteCommunicationLiteFabric(TTDevice* local_tt_device, SysmemManager* sysmem_manager = nullptr);

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
    lite_fabric::HostToLiteFabricInterface<lite_fabric::SENDER_NUM_BUFFERS_ARRAY[0], lite_fabric::CHANNEL_BUFFER_SIZE>
        host_interface;
};

}  // namespace tt::umd
