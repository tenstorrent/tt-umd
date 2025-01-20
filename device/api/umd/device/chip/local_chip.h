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

    LocalChip(tt_SocDescriptor soc_descriptor, std::unique_ptr<TTDevice> tt_device, const ChipInfo chip_info);

    TTDevice* get_tt_device() override;

    bool is_mmio_capable() const override;

private:
    void initialize_tlb_manager();

    std::unique_ptr<TTDevice> tt_device_;
};
}  // namespace tt::umd
