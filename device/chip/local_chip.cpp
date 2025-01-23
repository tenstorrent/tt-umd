/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

#include "umd/device/tt_device/tlb_manager.h"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

void LocalChip::initialize_tlb_manager() {
    auto tlb_manager = tt_device_->get_tlb_manager();
    // Setup default dynamic tlbs.
    tlb_manager->set_dynamic_tlb_config(
        "LARGE_READ_TLB", tt_device_->get_architecture_implementation()->get_mem_large_read_tlb());
    tlb_manager->set_dynamic_tlb_config(
        "LARGE_WRITE_TLB", tt_device_->get_architecture_implementation()->get_mem_large_write_tlb());
    tlb_manager->set_dynamic_tlb_config("REG_TLB", tt_device_->get_architecture_implementation()->get_reg_tlb());
    tlb_manager->set_dynamic_tlb_config(
        "SMALL_READ_WRITE_TLB", tt_device_->get_architecture_implementation()->get_small_read_write_tlb());
}

LocalChip::LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id) :
    Chip(soc_descriptor), tt_device_(TTDevice::create(pci_device_id)) {
    initialize_tlb_manager();
}

LocalChip::LocalChip(std::unique_ptr<TTDevice> tt_device, const ChipInfo chip_info) :
    Chip(
        tt_SocDescriptor(
            tt_SocDescriptor::get_soc_descriptor_path(
                tt_device->get_arch(), chip_info.board_type, chip_info.asic_location),
            chip_info.noc_translation_enabled,
            chip_info.tensix_harvesting_mask,
            chip_info.dram_harvesting_mask,
            chip_info.eth_harvesting_mask),
        chip_info),
    tt_device_(std::move(tt_device)) {
    initialize_tlb_manager();
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

bool LocalChip::is_mmio_capable() const { return true; }

}  // namespace tt::umd
