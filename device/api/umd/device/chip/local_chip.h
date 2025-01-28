/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"

namespace tt::umd {

class LocalChip : public Chip {
public:
    LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id);

    LocalChip(std::unique_ptr<TTDevice> tt_device, const ChipInfo chip_info);

    TTDevice* get_tt_device() override;

    bool is_mmio_capable() const override;

    void wait_eth_cores_training(const uint32_t timeout_per_core = 1000) override;

private:
    std::unique_ptr<TTDevice> tt_device_;

    void initialize_tlb_manager();
};
}  // namespace tt::umd
