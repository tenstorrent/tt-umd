/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <set>
#include <unordered_set>

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
        eth_coord_t target_chip,
        tt_xy_pair target_core,
        const void* src,
        uint64_t core_dest,
        uint32_t size_in_bytes,
        bool broadcast = false,
        std::vector<int> broadcast_header = {});

    void wait_for_non_mmio_flush();

    // Set the ethernet cores which can be used for remote communication on the assigned local chip.
    void set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores);
    void set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels);

private:
    CoreCoord get_remote_transfer_ethernet_core();
    void update_active_eth_core_idx();

    std::vector<CoreCoord> remote_transfer_eth_cores_;
    int active_eth_core_idx = 0;
    bool flush_non_mmio_ = false;

    LocalChip* local_chip_;
    LockManager lock_manager_;
};

}  // namespace tt::umd
