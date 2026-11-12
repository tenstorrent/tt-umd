// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <unordered_set>
#include <vector>

#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/lock_manager.hpp"
#include "tt-umd/utils/timeouts.hpp"

namespace tt {
struct EthCoord;
}  // namespace tt

namespace tt::umd {

class HostMemChannels;
class TTDevice;

class RemoteCommunication {
public:
    RemoteCommunication(TTDevice* local_tt_device, HostMemChannels* host_mem_channels = nullptr);
    virtual ~RemoteCommunication() = default;

    static std::unique_ptr<RemoteCommunication> create_remote_communication(
        TTDevice* local_tt_device, EthCoord target_chip, HostMemChannels* host_mem_channels = nullptr);

    // Target core should be in translated coords.
    virtual void read_non_mmio(
        tt_xy_pair target_core,
        void* dest,
        uint64_t core_src,
        uint32_t size_in_bytes,
        const std::chrono::milliseconds timeout_ms = timeout::NON_MMIO_RW_TIMEOUT) = 0;

    virtual void write_to_non_mmio(
        tt_xy_pair target_core,
        const void* src,
        uint64_t core_dest,
        uint32_t size_in_bytes,
        bool broadcast = false,
        std::vector<int> broadcast_header = {},
        const std::chrono::milliseconds timeout_ms = timeout::NON_MMIO_RW_TIMEOUT) = 0;

    virtual void wait_for_non_mmio_flush(const std::chrono::milliseconds timeout_ms = timeout::NON_MMIO_RW_TIMEOUT) = 0;

    // Set the ethernet cores which can be used for remote communication on the assigned local chip.
    // The cores should be in translated coordinates.
    void set_remote_transfer_ethernet_cores(const std::unordered_set<tt_xy_pair>& cores);

    // The ethernet cores remote transfers are spread over, in translated coordinates.
    const std::vector<tt_xy_pair>& get_remote_transfer_ethernet_cores() const;

    TTDevice* get_local_device();

    virtual std::optional<EthCoord> get_target_eth_coord() = 0;

    // Get the active eth core that will be used for the next remote communication.
    // Which core is used for remote communication can change.
    tt_xy_pair get_remote_transfer_ethernet_core();

    // Sets the local chip's host memory channels, used for host-DRAM-backed transfers (required for broadcasts and
    // used to speed up large transfers).
    void set_host_mem_channels(HostMemChannels* host_mem_channels);

    bool has_host_mem_channels() const;

protected:
    void update_active_eth_core_idx();

    std::vector<tt_xy_pair> remote_transfer_eth_cores_;
    int active_eth_core_idx = 0;
    bool flush_non_mmio_ = false;

    TTDevice* local_tt_device_;

    HostMemChannels* host_mem_channels_;
};

}  // namespace tt::umd
