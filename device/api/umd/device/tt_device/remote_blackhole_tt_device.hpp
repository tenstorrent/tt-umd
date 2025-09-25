/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/local_chip.hpp"
#include "umd/device/tt_device/blackhole_tt_device.hpp"

namespace tt::umd {

class RemoteBlackholeTTDevice : public BlackholeTTDevice {
public:
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void wait_for_non_mmio_flush() override;

    RemoteCommunication* get_remote_communication();

    bool wait_arc_post_reset(const uint32_t timeout_ms = 1000) override;

private:
    RemoteBlackholeTTDevice(std::unique_ptr<RemoteCommunication> remote_communication);

    friend std::unique_ptr<TTDevice> TTDevice::create(
        std::unique_ptr<RemoteCommunication> remote_communication, eth_coord_t target_chip);

    std::unique_ptr<RemoteCommunication> remote_communication_;
};

}  // namespace tt::umd
