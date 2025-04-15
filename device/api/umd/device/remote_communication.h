/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

class LocalChip;

class RemoteCommunication {
public:
    RemoteCommunication(LocalChip* local_chip);
    virtual ~RemoteCommunication();

    // Target core should be in translated coords.
    void read_non_mmio(
        eth_coord_t target_chip, tt_xy_pair target_core, void* dest, uint64_t core_src, uint32_t size_in_bytes);

    void write_to_non_mmio(
        uint8_t* mem_ptr,
        tt_xy_pair core,
        uint64_t address,
        uint32_t size_in_bytes,
        eth_coord_t target_chip,
        const tt_xy_pair eth_core);

    void wait_for_non_mmio_flush();

private:
    LocalChip* local_chip_;
};

}  // namespace tt::umd
