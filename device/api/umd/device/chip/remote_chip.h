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
    bool is_mmio_capable() const override;

    void write_to_device(
        tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size, const std::string& fallback_tlb) override;
    void read_from_device(
        tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size, const std::string& fallback_tlb) override;

    void wait_for_non_mmio_flush() override;

private:
    tt_xy_pair translate_chip_coord_virtual_to_translated(const tt_xy_pair core);

    eth_coord_t eth_chip_location_;
    std::unique_ptr<RemoteCommunication> remote_communication_;
};
}  // namespace tt::umd
