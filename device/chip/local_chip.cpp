/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.h"

#include "logger.hpp"
#include "umd/device/blackhole_implementation.h"
#include "umd/device/chip_helpers/tlb_manager.h"
#include "umd/device/tt_device/tt_device.h"
#include "umd/device/types/blackhole_eth.h"

extern bool umd_use_noc1;

namespace tt::umd {

// TLB size for DRAM on blackhole - 4GB
const uint64_t BH_4GB_TLB_SIZE = 4ULL * 1024 * 1024 * 1024;

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

    size_t num_eth_cores = soc_descriptor_.get_num_eth_channels();
    auto tensix_grid = soc_descriptor_.get_grid_size(CoreType::TENSIX);

    // Setup static TLBs for all eth cores
    for (const CoreCoord& virtual_core : soc_descriptor_.get_cores(CoreType::ETH, CoordSystem::VIRTUAL)) {
        CoreCoord translated_core = soc_descriptor_.translate_coord_to(virtual_core, CoordSystem::TRANSLATED);
        CoreCoord logical_core = soc_descriptor_.translate_coord_to(virtual_core, CoordSystem::LOGICAL);

        log_assert(
            logical_core.x == 0, "Logical core x coord should be always 0 for ETH cores, but got {}", logical_core.x);

        size_t tlb_index = logical_core.y;
        tlb_manager_->configure_tlb(virtual_core, translated_core, tlb_index, 0, tlb_data::Strict);
    }

    // Setup static TLBs for all worker cores
    for (const CoreCoord& virtual_core : soc_descriptor_.get_cores(CoreType::TENSIX, CoordSystem::VIRTUAL)) {
        CoreCoord translated_core = soc_descriptor_.translate_coord_to(virtual_core, CoordSystem::TRANSLATED);
        CoreCoord logical_core = soc_descriptor_.translate_coord_to(virtual_core, CoordSystem::LOGICAL);

        size_t tlb_index = num_eth_cores + logical_core.x + logical_core.y * tensix_grid.x;
        // TODO
        // Note: see tt_metal issue #10107
        // Strict is less performant than Posted, however, metal doesn't presently
        // use this on a perf path and the launch_msg "kernel config" needs to
        // arrive prior to the "go" message during device init and slow dispatch
        // Revisit this when we have a more flexible UMD api
        tlb_manager_->configure_tlb(virtual_core, translated_core, tlb_index, 0, tlb_data::Strict);
    }

    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        // Setup static TLBs for MMIO mapped data space in DRAM.
        CoreCoord virtual_core = soc_descriptor_.get_dram_core_for_channel(0, 0, CoordSystem::VIRTUAL);
        CoreCoord translated_core = soc_descriptor_.translate_coord_to(virtual_core, CoordSystem::TRANSLATED);
        architecture_implementation* arch_impl = tt_device_->get_architecture_implementation();
        uint64_t peer_dram_offset = arch_impl->get_dram_channel_0_peer2peer_region_start();
        for (uint32_t tlb_id = arch_impl->get_dynamic_tlb_base_index();
             tlb_id < arch_impl->get_dynamic_tlb_base_index() + arch_impl->get_dynamic_tlb_count();
             tlb_id++) {
            tlb_manager_->configure_tlb(virtual_core, translated_core, tlb_id, peer_dram_offset, tlb_data::Relaxed);
            // Align address space of 16MB TLB to 16MB boundary
            peer_dram_offset += arch_impl->get_dynamic_tlb_16m_size();
        }
    } else {
        // Setup static 4GB tlbs for DRAM cores
        for (uint32_t dram_channel = 0; dram_channel < soc_descriptor_.get_num_dram_channels(); dram_channel++) {
            CoreCoord virtual_core = soc_descriptor_.get_dram_core_for_channel(dram_channel, 0, CoordSystem::VIRTUAL);
            CoreCoord translated_core = soc_descriptor_.translate_coord_to(virtual_core, CoordSystem::TRANSLATED);

            auto tlb_index = tt_device_->get_architecture_implementation()->get_tlb_4g_base_index() + dram_channel;
            tlb_manager_->configure_tlb(virtual_core, translated_core, tlb_index, 0, tlb_data::Posted);
        }
    }
}

void LocalChip::initialize_default_chip_mutexes(const bool clear_mutex) {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of
    // time here (during device init) since it's unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is
    // responsible for initializing the driver with this field set to cleanup after an aborted process.
    int pci_device_id = tt_device_->get_pci_device()->get_device_num();
    // Initialize Dynamic TLB mutexes
    for (auto& tlb : tlb_manager_->dynamic_tlb_config_) {
        lock_manager_.initialize_mutex(tlb.first, pci_device_id, clear_mutex);
    }

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        lock_manager_.initialize_mutex(MutexType::NON_MMIO, pci_device_id, clear_mutex);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic
    lock_manager_.initialize_mutex(MutexType::MEM_BARRIER, pci_device_id, clear_mutex);
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

void LocalChip::write_to_device(
    tt_xy_pair core, const void* src, uint64_t l1_dest, uint32_t size, const std::string& fallback_tlb) {
    const uint8_t* buffer_addr = static_cast<const uint8_t*>(src);

    log_debug(
        LogSiliconDriver,
        "Chip::write_to_device to pci dev {} core {}-{} at 0x{:x} size: {}",
        tt_device_->get_pci_device()->get_device_num(),
        core.x,
        core.y,
        l1_dest,
        size);

    if (tlb_manager_->is_tlb_mapped(core, l1_dest, size)) {
        tlb_configuration tlb_description = tlb_manager_->get_tlb_configuration(core);
        if (tt_device_->get_pci_device()->bar4_wc != nullptr && tlb_description.size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  write to DRAM (BAR4 space), we add offset
            // to which we write so write_block knows it needs to target BAR4
            tt_device_->write_block(
                (tlb_description.tlb_offset + l1_dest % tlb_description.size) + BAR0_BH_SIZE, size, buffer_addr);
        } else {
            tt_device_->write_block(tlb_description.tlb_offset + l1_dest % tlb_description.size, size, buffer_addr);
        }
    } else {
        const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
        auto lock = acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());

        while (size > 0) {
            auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(
                tlb_index,
                translate_chip_coord_virtual_to_translated(core),
                l1_dest,
                tlb_manager_->dynamic_tlb_ordering_modes_.at(fallback_tlb));
            uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
            tt_device_->write_block(mapped_address, transfer_size, buffer_addr);

            size -= transfer_size;
            l1_dest += transfer_size;
            buffer_addr += transfer_size;
        }
        log_debug(LogSiliconDriver, "Write done Dynamic TLB with pid={}", (long)getpid());
    }
}

void LocalChip::read_from_device(
    tt_xy_pair core, void* dest, uint64_t l1_src, uint32_t size, const std::string& fallback_tlb) {
    log_debug(
        LogSiliconDriver,
        "Chip::read_from_device from pci device {} core {}-{} at 0x{:x} size: {}",
        tt_device_->get_pci_device()->get_device_num(),
        core.x,
        core.y,
        l1_src,
        size);
    uint8_t* buffer_addr = static_cast<uint8_t*>(dest);

    if (tlb_manager_->is_tlb_mapped(core, l1_src, size)) {
        tlb_configuration tlb_description = tlb_manager_->get_tlb_configuration(core);
        if (tt_device_->get_pci_device()->bar4_wc != nullptr && tlb_description.size == BH_4GB_TLB_SIZE) {
            // This is only for Blackhole. If we want to  read from DRAM (BAR4 space), we add offset
            // from which we read so read_block knows it needs to target BAR4
            tt_device_->read_block(
                (tlb_description.tlb_offset + l1_src % tlb_description.size) + BAR0_BH_SIZE, size, buffer_addr);
        } else {
            tt_device_->read_block(tlb_description.tlb_offset + l1_src % tlb_description.size, size, buffer_addr);
        }
        log_debug(
            LogSiliconDriver,
            "  read_block called with tlb_offset: {}, tlb_size: {}",
            tlb_description.tlb_offset,
            tlb_description.size);
    } else {
        const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
        auto lock = acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());
        log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);
        while (size > 0) {
            auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(
                tlb_index,
                translate_chip_coord_virtual_to_translated(core),
                l1_src,
                tlb_manager_->dynamic_tlb_ordering_modes_.at(fallback_tlb));
            uint32_t transfer_size = std::min((uint64_t)size, tlb_size);
            tt_device_->read_block(mapped_address, transfer_size, buffer_addr);

            size -= transfer_size;
            l1_src += transfer_size;
            buffer_addr += transfer_size;
        }
        log_debug(LogSiliconDriver, "Read done Dynamic TLB with pid={}", (long)getpid());
    }
}

void LocalChip::write_to_device_reg(
    tt_xy_pair core, const void* src, uint64_t reg_dest, uint32_t size, const std::string& fallback_tlb) {
    if (size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Size must be a multiple of 4 bytes");
    }

    if (reg_dest % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Register address must be 4-byte aligned");
    }

    const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
    auto lock = lock_manager_.acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());
    log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);

    auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(
        tlb_index, translate_chip_coord_virtual_to_translated(core), reg_dest, tt::umd::tlb_data::Strict);
    tt_device_->write_regs(mapped_address, size / sizeof(uint32_t), src);
}

void LocalChip::read_from_device_reg(
    tt_xy_pair core, void* dest, uint64_t reg_src, uint32_t size, const std::string& fallback_tlb) {
    if (size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Size must be a multiple of 4 bytes");
    }

    if (reg_src % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Register address must be 4-byte aligned");
    }

    const auto tlb_index = tlb_manager_->dynamic_tlb_config_.at(fallback_tlb);
    auto lock = lock_manager_.acquire_mutex(fallback_tlb, tt_device_->get_pci_device()->get_device_num());
    log_debug(LogSiliconDriver, "  dynamic tlb_index: {}", tlb_index);

    auto [mapped_address, tlb_size] = tt_device_->set_dynamic_tlb(
        tlb_index, translate_chip_coord_virtual_to_translated(core), reg_src, tt::umd::tlb_data::Strict);
    tt_device_->read_regs(mapped_address, size / sizeof(uint32_t), dest);
}

tt_xy_pair LocalChip::translate_chip_coord_virtual_to_translated(const tt_xy_pair core) const {
    CoreCoord core_coord = soc_descriptor_.get_coord_at(core, CoordSystem::VIRTUAL);
    // Since NOC1 and translated coordinate space overlaps for Tensix cores on Blackhole,
    // Tensix cores are always used in translated space. Other cores are used either in
    // NOC1 or translated space depending on the umd_use_noc1 flag.
    // On Wormhole Tensix can use NOC1 space if umd_use_noc1 is set to true.
    if (soc_descriptor_.noc_translation_enabled) {
        if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
            if (core_coord.core_type == CoreType::TENSIX || !umd_use_noc1) {
                return soc_descriptor_.translate_coord_to(core_coord, CoordSystem::TRANSLATED);
            } else {
                return soc_descriptor_.translate_coord_to(core_coord, CoordSystem::NOC1);
            }
        } else {
            return soc_descriptor_.translate_coord_to(
                core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
        }
    } else {
        return soc_descriptor_.translate_coord_to(
            core_coord, umd_use_noc1 ? CoordSystem::NOC1 : CoordSystem::TRANSLATED);
    }
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(std::string mutex_name, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_name, pci_device_id);
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(MutexType mutex_type, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_type, pci_device_id);
}

void LocalChip::wait_dram_cores_training(const uint32_t timeout_ms) {
    if (get_tt_device()->get_arch() == tt::ARCH::BLACKHOLE) {
        return;
    }

    TTDevice* tt_device = get_tt_device();

    auto start = std::chrono::system_clock::now();
    while (true) {
        std::vector<DramTrainingStatus> dram_training_status = tt_device->get_dram_training_status();

        if (dram_training_status.empty()) {
            // DRAM training status is not available, breaking the wait for DRAM training.
            break;
        }

        bool all_dram_channels_trained = true;
        const uint32_t chip_num_dram_channels =
            std::min(dram_training_status.size(), get_soc_descriptor().get_dram_cores().size());
        const uint32_t dram_harvesting_mask = get_soc_descriptor().harvesting_masks.dram_harvesting_mask;
        for (uint32_t dram_channel = 0; dram_channel < chip_num_dram_channels; dram_channel++) {
            // Skip the check for harvested channels.
            if (dram_harvesting_mask & (1 << dram_channel)) {
                continue;
            }

            // Check if there is an error in training for the channel.
            if (dram_training_status[dram_channel] == DramTrainingStatus::FAIL) {
                throw std::runtime_error("DRAM training failed");
            }

            // Verify whether the channel is trained.
            all_dram_channels_trained &= (dram_training_status[dram_channel] == DramTrainingStatus::SUCCESS);
        }

        if (all_dram_channels_trained) {
            break;
        }

        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() > timeout_ms) {
            throw std::runtime_error(fmt::format("DRAM training timed out after {} ms", timeout_ms));
            break;
        }
    }
}

}  // namespace tt::umd
