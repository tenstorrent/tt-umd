/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

class RemoteCommunication {
public:
    RemoteCommunication(TTDevice* tt_device);
    virtual ~RemoteCommunication();

    void read_non_mmio(
        uint8_t* mem_ptr,
        tt_xy_pair core,
        uint64_t address,
        uint32_t size_in_bytes,
        eth_coord_t target_chip,
        const tt_xy_pair eth_core);

    void write_to_non_mmio(
        uint8_t* mem_ptr,
        tt_xy_pair core,
        uint64_t address,
        uint32_t size_in_bytes,
        eth_coord_t target_chip,
        const tt_xy_pair eth_core);

    void wait_for_non_mmio_flush(std::vector<tt_xy_pair> remote_transfer_eth_cores);

private:
    TTDevice* tt_device;
    LockManager lock_manager;
};

}  // namespace tt::umd
