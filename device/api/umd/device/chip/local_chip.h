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

    LocalChip(std::unique_ptr<TTDevice> tt_device);

    TTDevice* get_tt_device() override;

    bool is_mmio_capable() const override;

private:
    std::unique_ptr<TTDevice> tt_device_;

    void initialize_local_chip();

    void initialize_tlb_manager();

protected:
    void wait_eth_cores_training(const uint32_t timeout_ms = 5000) override;
};
}  // namespace tt::umd
