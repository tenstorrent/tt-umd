/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

#include "logger.hpp"
#include "umd/device/chip_helpers/tlb_manager.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/blackhole_eth.h"

namespace tt::umd {

LocalChip::LocalChip(
    tt_SocDescriptor soc_descriptor, int pci_device_id, int num_host_mem_channels, const bool clear_mutex) :
    Chip(soc_descriptor),
    tt_device_(TTDevice::create(pci_device_id)),
    sysmem_manager_(std::make_unique<SysmemManager>(tt_device_.get())),
    tlb_manager_(std::make_unique<TLBManager>(tt_device_.get())) {
    initialize_local_chip(num_host_mem_channels, clear_mutex);
}

LocalChip::LocalChip(std::string sdesc_path, std::unique_ptr<TTDevice> tt_device) :
    Chip(
        tt_device->get_chip_info(),
        tt_SocDescriptor(
            sdesc_path,
            tt_device->get_chip_info().noc_translation_enabled,
            tt_device->get_chip_info().harvesting_masks,
            tt_device->get_chip_info().board_type)),
    tt_device_(std::move(tt_device)),
    sysmem_manager_(std::make_unique<SysmemManager>(tt_device_.get())),
    tlb_manager_(std::make_unique<TLBManager>(tt_device_.get())) {
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
    tt_device_(std::move(tt_device)),
    sysmem_manager_(std::make_unique<SysmemManager>(tt_device_.get())),
    tlb_manager_(std::make_unique<TLBManager>(tt_device_.get())) {
    initialize_local_chip();
}

void LocalChip::initialize_local_chip(int num_host_mem_channels, const bool clear_mutex) {
    initialize_tlb_manager();
    if (num_host_mem_channels > 0) {
        sysmem_manager_->init_hugepage(num_host_mem_channels);
    }
    wait_chip_to_be_ready();
    initialize_default_chip_mutexes(clear_mutex);
}

void LocalChip::initialize_tlb_manager() {
    // Setup default dynamic tlbs.
    tlb_manager_->set_dynamic_tlb_config(
        "LARGE_READ_TLB", tt_device_->get_architecture_implementation()->get_mem_large_read_tlb());
    tlb_manager_->set_dynamic_tlb_config(
        "LARGE_WRITE_TLB", tt_device_->get_architecture_implementation()->get_mem_large_write_tlb());
    tlb_manager_->set_dynamic_tlb_config("REG_TLB", tt_device_->get_architecture_implementation()->get_reg_tlb());
    tlb_manager_->set_dynamic_tlb_config(
        "SMALL_READ_WRITE_TLB", tt_device_->get_architecture_implementation()->get_small_read_write_tlb());
}

void LocalChip::initialize_default_chip_mutexes(const bool clear_mutex) {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of
    // time here (during device init) since it's unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is
    // responsible for initializing the driver with this field set to cleanup after an aborted process.
    int pci_device_id = tt_device_->get_pci_device()->get_device_num();
    // Initialize Dynamic TLB mutexes
    for (auto& tlb : tlb_manager_->dynamic_tlb_config_) {
        lock_manager.initialize_mutex(tlb.first, pci_device_id, clear_mutex);
    }

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        lock_manager.initialize_mutex(MutexType::NON_MMIO, pci_device_id, clear_mutex);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic
    lock_manager.initialize_mutex(MutexType::MEM_BARRIER, pci_device_id, clear_mutex);
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* LocalChip::get_sysmem_manager() { return sysmem_manager_.get(); }

TLBManager* LocalChip::get_tlb_manager() { return tlb_manager_.get(); }

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

void LocalChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    sysmem_manager_->write_to_sysmem(channel, src, sysmem_dest, size);
}

void LocalChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    sysmem_manager_->read_from_sysmem(channel, dest, sysmem_src, size);
}

std::unique_lock<boost::interprocess::named_mutex> LocalChip::get_mutex(std::string mutex_name, int pci_device_id) {
    return lock_manager.get_mutex(mutex_name, pci_device_id);
}

std::unique_lock<boost::interprocess::named_mutex> LocalChip::get_mutex(MutexType mutex_type, int pci_device_id) {
    return lock_manager.get_mutex(mutex_type, pci_device_id);
}
}  // namespace tt::umd
