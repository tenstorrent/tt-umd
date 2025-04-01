/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "umd/device/chip/chip.h"
#include "umd/device/chip_helpers/sysmem_manager.h"
#include "umd/device/chip_helpers/tlb_manager.h"

namespace tt::umd {

class LocalChip : public Chip {
public:
    LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id, int num_host_mem_channels = 0);

    LocalChip(std::string sdesc_path, std::unique_ptr<TTDevice> tt_device);

    LocalChip(std::unique_ptr<TTDevice> tt_device);

    bool is_mmio_capable() const override;

    TTDevice* get_tt_device() override;
    SysmemManager* get_sysmem_manager() override;
    TLBManager* get_tlb_manager() override;

    void write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) override;
    void read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) override;

private:
    std::unique_ptr<TTDevice> tt_device_;
    std::unique_ptr<SysmemManager> sysmem_manager_;
    std::unique_ptr<TLBManager> tlb_manager_;

    void initialize_local_chip(int num_host_mem_channels = 0);

    void initialize_tlb_manager();

protected:
    void wait_eth_cores_training(const uint32_t timeout_ms = 60000) override;
};
}  // namespace tt::umd
