/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/chip/local_chip.h"

namespace tt::umd {
class RemoteTTDevice : public TTDevice {
public:
    RemoteTTDevice(LocalChip* local_chip, eth_coord_t target_chip);

    void wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms = 1000) override;

    ChipInfo get_chip_info() override;

    uint32_t get_clock() override;

    uint32_t get_max_clock_freq() override;

    uint32_t get_min_clock_freq() override;

    BoardType get_board_type() override;

    std::vector<DramTrainingStatus> get_dram_training_status() override;

    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void write_to_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void dma_d2h(void* dst, uint32_t src, size_t size) override;

    void dma_h2d(uint32_t dst, const void* src, size_t size) override;

    void dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) override;

    void dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) override;

    bool get_noc_translation_enabled() override;

    void wait_eth_core_training(const tt_xy_pair eth_core, const uint32_t timeout_ms = 60000) override;

private:
    LocalChip* local_chip_;
    eth_coord_t target_chip_;
    std::unique_ptr<RemoteCommunication> remote_communication_;
};
}  // namespace tt::umd
