/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"

namespace tt::umd {

class TLBManager;

class LocalChip : public Chip {
public:
    LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id);

    TTDevice* get_tt_device() override;
    TLBManager* get_tlb_manager() override;

    bool is_mmio_capable() const override;

private:
    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<TLBManager> tlb_manager_;
};
}  // namespace tt::umd
