/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

#include "logger.hpp"
#include "umd/device/tt_device/tlb_manager.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/blackhole_eth.h"

namespace tt::umd {

LocalChip::LocalChip(tt_SocDescriptor soc_descriptor, int pci_device_id) :
    Chip(soc_descriptor), tt_device_(TTDevice::create(pci_device_id)) {
    initialize_local_chip();
}

LocalChip::LocalChip(std::string sdesc_path, std::unique_ptr<TTDevice> tt_device) :
    Chip(
        tt_device->get_chip_info(),
        tt_SocDescriptor(
            sdesc_path,
            tt_device->get_chip_info().noc_translation_enabled,
            tt_device->get_chip_info().harvesting_masks,
            tt_device->get_chip_info().board_type)),
    tt_device_(std::move(tt_device)) {
    initialize_local_chip();
}

LocalChip::LocalChip(std::unique_ptr<TTDevice> tt_device) :
    Chip(
        tt_device->get_chip_info(),
        tt_SocDescriptor(
            tt_device->get_arch(),
            tt_device->get_chip_info().noc_translation_enabled,
            tt_device->get_chip_info().harvesting_masks,
            tt_device->get_chip_info().board_type)),
    tt_device_(std::move(tt_device)) {
    initialize_local_chip();
}

void LocalChip::initialize_local_chip() {
    initialize_tlb_manager();
    wait_chip_to_be_ready();
}

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

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

bool LocalChip::is_mmio_capable() const { return true; }

void LocalChip::wait_eth_cores_training(const uint32_t timeout_ms) {
    if (get_tt_device()->get_arch() != tt::ARCH::BLACKHOLE) {
        return;
    }

    const std::vector<CoreCoord> eth_cores = get_soc_descriptor().get_cores(CoreType::ETH);
    TTDevice* tt_device = get_tt_device();
    auto start = std::chrono::system_clock::now();
    for (const CoreCoord& eth_core : eth_cores) {
        const tt_xy_pair eth_core_pair = {eth_core.x, eth_core.y};

        uint32_t port_status_addr = blackhole::BOOT_RESULTS_ADDR + offsetof(blackhole::eth_status_t, port_status);
        uint32_t port_status_val;
        tt_device->read_from_device(&port_status_val, eth_core_pair, port_status_addr, sizeof(port_status_val));

        // Port status should be last state to settle during the eth training sequence
        // PORT_UNKNOWN means that eth is still training
        while (port_status_val == blackhole::port_status_e::PORT_UNKNOWN) {
            tt_device->read_from_device(&port_status_val, eth_core_pair, port_status_addr, sizeof(port_status_val));
            auto end = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            if (duration.count() > timeout_ms) {
                // TODO: Exception should be thrown here. ETH connections are very flaky
                // on Blackhole right now. When this is fixed we can throw the exception here.
                // Since we are not going to do any remote IO at the moment it is fine to just log the error.
                log_error("ETH training timed out after {} ms", timeout_ms);
                break;
            }
        }
    }
}

}  // namespace tt::umd
