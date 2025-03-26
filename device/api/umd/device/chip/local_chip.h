/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/chip_helpers/sysmem_manager.h"

namespace tt::umd {

class LocalChip : public Chip {
public:
    LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id);

    LocalChip(std::string sdesc_path, std::unique_ptr<TTDevice> tt_device);

    LocalChip(std::unique_ptr<TTDevice> tt_device);

    bool is_mmio_capable() const override;

    TTDevice* get_tt_device() override;

    void write_to_sysmem(const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel) override;
    void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size) override;

private:
    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<SysmemManager> sysmem_manager_;

    void initialize_local_chip();

    void initialize_tlb_manager();

protected:
    void wait_eth_cores_training(const uint32_t timeout_ms = 60000) override;
};
}  // namespace tt::umd
