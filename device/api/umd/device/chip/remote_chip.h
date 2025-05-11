/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/remote_communication.h"

namespace tt::umd {
class LocalChip;

class RemoteChip : public Chip {
public:
    RemoteChip(tt_SocDescriptor soc_descriptor, eth_coord_t eth_chip_location, LocalChip* local_chip);

    RemoteChip(tt_SocDescriptor soc_descriptor, ChipInfo chip_info);

    bool is_mmio_capable() const override;

    void start_device() override;
    void close_device() override;

    void write_to_device(tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size) override;

    void wait_for_non_mmio_flush() override;

    int arc_msg(
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override;

    void l1_membar(const std::unordered_set<tt::umd::CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<tt::umd::CoreCoord>& cores = {}) override;
    void dram_membar(const std::unordered_set<uint32_t>& channels = {}) override;

    void deassert_risc_resets() override;
    void set_power_state(tt_DevicePowerState state) override;
    int get_clock() override;

private:
    tt_xy_pair translate_chip_coord_virtual_to_translated(const tt_xy_pair core);

    eth_coord_t eth_chip_location_;
    std::unique_ptr<RemoteCommunication> remote_communication_;
    LocalChip* local_chip_;
};
}  // namespace tt::umd
