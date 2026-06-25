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

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt {
struct EthCoord;
}  // namespace tt

namespace tt::umd {

class SysmemManager;
class TTDevice;

class RemoteCommunication {
public:
    RemoteCommunication(TTDevice* local_tt_device, SysmemManager* sysmem_manager = nullptr);
    virtual ~RemoteCommunication() = default;

    static std::unique_ptr<RemoteCommunication> create_remote_communication(
        TTDevice* local_tt_device, EthCoord target_chip, SysmemManager* sysmem_manager = nullptr);

    // Target core should be in translated coords.
    // Note that since we're not using TLBManager, the read/writes won't ever go through static TLBs, which should
    // probably be redesigned in some way.
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

    TTDevice* get_local_device();

    virtual std::optional<EthCoord> get_target_eth_coord() = 0;

    // Get the active eth core that will be used for the next remote communication.
    // Which core is used for remote communication can change.
    tt_xy_pair get_remote_transfer_ethernet_core();

    // Sets the sysmem manager used for host-DRAM-backed transfers (required for broadcasts and used to speed up
    // large transfers).
    void set_sysmem_manager(SysmemManager* sysmem_manager);

    bool has_sysmem_manager() const;

protected:
    void update_active_eth_core_idx();

    std::vector<tt_xy_pair> remote_transfer_eth_cores_;
    int active_eth_core_idx = 0;
    bool flush_non_mmio_ = false;

    TTDevice* local_tt_device_;

    LockManager lock_manager_;
    SysmemManager* sysmem_manager_;
};

}  // namespace tt::umd
