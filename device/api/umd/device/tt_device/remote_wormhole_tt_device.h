/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/local_chip.h"
#include "umd/device/tt_device/wormhole_tt_device.h"

namespace tt::umd {

class RemoteWormholeTTDevice : public WormholeTTDevice {
public:
    RemoteWormholeTTDevice(LocalChip* local_chip, eth_coord_t target_chip);

    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_arc(void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc(const void* mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void wait_for_non_mmio_flush() override;

    LocalChip* get_local_chip();

    RemoteCommunication* get_remote_communication();

private:
    LocalChip* local_chip_;
    eth_coord_t target_chip_;
    std::unique_ptr<RemoteCommunication> remote_communication_;
};

}  // namespace tt::umd
