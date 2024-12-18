/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

#include "chip/tlb_manager.h"
#include "umd/device/tt_device/tt_device.h"

namespace tt::umd {

LocalChip::LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id) :
    Chip(soc_descriptor),
    tt_device_(TTDevice::create(pci_device_id)),
    tlb_manager_(std::make_unique<TLBManager>(tt_device_.get())) {
    // Setup default dynamic tlbs.
    tlb_manager_->set_dynamic_tlb_config(
        "LARGE_READ_TLB", tt_device_->get_architecture_implementation()->get_mem_large_read_tlb());
    tlb_manager_->set_dynamic_tlb_config_ordering("LARGE_READ_TLB", tlb_data::Relaxed);
    tlb_manager_->set_dynamic_tlb_config(
        "LARGE_WRITE_TLB", tt_device_->get_architecture_implementation()->get_mem_large_write_tlb());
    tlb_manager_->set_dynamic_tlb_config_ordering("LARGE_WRITE_TLB", tlb_data::Relaxed);
    tlb_manager_->set_dynamic_tlb_config("REG_TLB", tt_device_->get_architecture_implementation()->get_reg_tlb());
    tlb_manager_->set_dynamic_tlb_config_ordering("REG_TLB", tlb_data::Relaxed);
    tlb_manager_->set_dynamic_tlb_config(
        "SMALL_READ_WRITE_TLB", tt_device_->get_architecture_implementation()->get_small_read_write_tlb());
    tlb_manager_->set_dynamic_tlb_config_ordering("SMALL_READ_WRITE_TLB", tlb_data::Relaxed);
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

TLBManager* LocalChip::get_tlb_manager() { return tlb_manager_.get(); }

bool LocalChip::is_mmio_capable() const { return true; }

}  // namespace tt::umd
