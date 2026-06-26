// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/local_chip.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "noc_access.hpp"
#include "tracy.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip_helpers/silicon_sysmem_manager.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/silicon_tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<LocalChip>(), "LocalChip must be non-abstract.");

std::unique_ptr<LocalChip> LocalChip::create(std::unique_ptr<TTDevice> tt_device, int num_host_mem_channels) {
    std::unique_ptr<TLBManager> tlb_manager = nullptr;
    std::unique_ptr<SysmemManager> sysmem_manager = nullptr;

    if (tt_device == nullptr) {
        UMD_THROW(error::RuntimeError, "Cannot create LocalChip without a TTDevice.");
    }

    // The variables below are only needed when using PCIe.
    // JTAG(currently the only communication protocol other than PCIe) has no use of them.
    if (tt_device->get_pci_device() != nullptr) {
        tlb_manager = std::make_unique<TLBManager>(tt_device.get());
        sysmem_manager = std::make_unique<SiliconSysmemManager>(tt_device.get(), num_host_mem_channels);
    }

    return std::unique_ptr<LocalChip>(
        new LocalChip(std::move(tt_device), std::move(tlb_manager), std::move(sysmem_manager)));
}

LocalChip::LocalChip(
    std::unique_ptr<TTDevice> tt_device,
    std::unique_ptr<TLBManager> tlb_manager,
    std::unique_ptr<SysmemManager> sysmem_manager) :
    Chip(tt_device->get_chip_info(), tt_device->get_arch()),
    tlb_manager_(std::move(tlb_manager)),
    sysmem_manager_(std::move(sysmem_manager)),
    tt_device_(std::move(tt_device)) {
    tt_device_->set_power_state(true);
    wait_chip_to_be_ready();
    if (tlb_manager_ != nullptr) {
        initialize_default_chip_mutexes();
    }
}

LocalChip::~LocalChip() {
    // Deconstruct the LocalChip in the right order.
    // TODO: Use intializers in constructor to avoid having to explicitly declare the order of destruction.
    tt_device_->set_power_state(false);
    cached_wc_tlb_window.reset();
    cached_uc_tlb_window.reset();
    sysmem_manager_.reset();
    tlb_manager_.reset();
    tt_device_.reset();
}

void LocalChip::initialize_default_chip_mutexes() {
    // These mutexes are intended to be based on physical devices/pci-intf not logical. Set these up ahead of
    // time here (during device init) since it's unsafe to modify shared state during multithreaded runtime.
    // cleanup_mutexes_in_shm is tied to clean_system_resources from the constructor. The main process is
    // responsible for initializing the driver with this field set to cleanup after an aborted process.
    int pci_device_id = tt_device_->get_pci_device()->get_device_num();

    // Initialize non-MMIO mutexes for WH devices regardless of number of chips, since these may be used for
    // ethernet broadcast
    if (tt_device_->get_arch() == tt::ARCH::WORMHOLE_B0) {
        lock_manager_.initialize_mutex(MutexType::REMOTE_ARC_MSG, pci_device_id);
    }

    // Initialize interprocess mutexes to make host -> device memory barriers atomic.
    lock_manager_.initialize_mutex(MutexType::MEM_BARRIER, pci_device_id);

    // Initialize mutex guarding initialized chips.
    lock_manager_.initialize_mutex(MutexType::CHIP_IN_USE, pci_device_id);
}

void LocalChip::initialize_membars(uint32_t dram_subchannel) {
    ZoneScopedC(tracy::Color::DarkGreen);
    set_membar_flag(
        get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED),
        MemBarFlag::RESET,
        l1_address_params.tensix_l1_barrier_base);
    set_membar_flag(
        get_soc_descriptor().get_cores(CoreType::ETH, CoordSystem::TRANSLATED),
        MemBarFlag::RESET,
        l1_address_params.eth_l1_barrier_base);

    std::vector<CoreCoord> dram_cores_vector = {};
    dram_cores_vector.reserve(get_soc_descriptor().get_num_dram_channels());
    for (std::uint32_t dram_idx = 0; dram_idx < get_soc_descriptor().get_num_dram_channels(); dram_idx++) {
        dram_cores_vector.push_back(
            get_soc_descriptor().get_dram_core_for_channel(dram_idx, dram_subchannel, CoordSystem::TRANSLATED));
    }
    set_membar_flag(dram_cores_vector, MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE);
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* LocalChip::get_sysmem_manager() { return sysmem_manager_.get(); }

TLBManager* LocalChip::get_tlb_manager() { return tlb_manager_.get(); }

bool LocalChip::is_mmio_capable() const { return true; }

void LocalChip::start_device(uint32_t dram_membar_subchannel) {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (tt_device_->get_communication_device_type() == IODeviceType::JTAG) {
        return;
    }

    // TODO: acquire mutex should live in Chip class. Currently we don't have unique id for all chips.
    // The lock here should suffice since we have to open Local chip to have Remote chips initialized.
    chip_started_lock_.emplace(acquire_mutex(MutexType::CHIP_IN_USE, tt_device_->get_pci_device()->get_device_num()));

    sysmem_manager_->pin_or_map_sysmem_to_device();
    if (!tt_device_->get_pci_device()->is_mapping_buffer_to_noc_supported()) {
        // If this is supported by the newer KMD, UMD doesn't have to program the iatu.
        init_pcie_iatus();
    }
    initialize_membars(dram_membar_subchannel);
}

void LocalChip::close_device() {
    ZoneScopedC(tracy::Color::DarkRed);
    // Investigating https://github.com/tenstorrent/tt-metal/issues/25377 found that closing device that was already put
    // in LONG_IDLE by tt-smi reset would hang
    if ((uint32_t)get_clock() != get_tt_device()->get_min_clock_freq()) {
        set_clock_state(DevicePowerState::LONG_IDLE);
        assert_risc_reset(RiscType::ALL);
        // Unmapping might be needed even in the case chip was reset due to kmd mappings.
        if (sysmem_manager_) {
            sysmem_manager_->unpin_or_unmap_sysmem();
        }
        if (tlb_manager_) {
            tlb_manager_->clear_mapped_tlbs();
        }
    }
    chip_started_lock_.reset();
};

int LocalChip::get_num_host_channels() {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    return sysmem_manager_->get_num_host_mem_channels();
}

int LocalChip::get_host_channel_size(std::uint32_t channel) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return 0;
    }

    UMD_ASSERT(
        channel < get_num_host_channels(),
        error::RuntimeError,
        "Querying size for a host channel that does not exist.");
    HugepageMapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
    UMD_ASSERT(
        hugepage_map.mapping_size,
        error::RuntimeError,
        "Host channel size can only be queried after the device has been started.");
    return hugepage_map.mapping_size;
}

void LocalChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    sysmem_manager_->write_to_sysmem(channel, src, sysmem_dest, size);
}

void LocalChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(
            LogUMD,
            "sysmem_manager was not initialized for {} communication protocol",
            DeviceTypeToString.at(tt_device_->get_communication_device_type()));
        return;
    }
    sysmem_manager_->read_from_sysmem(channel, dest, sysmem_src, size);
}

void LocalChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    log_trace(
        LogUMD,
        "Chip::write_to_device to {} dev {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_dest,
        size);

    tt_xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->write_to_device(src, translated_core, l1_dest, size);
        return;
    }

    if (tlb_manager_->is_tlb_mapped(translated_core, l1_dest, size)) {
        TlbWindow* tlb_window = tlb_manager_->get_tlb_window(translated_core);
        tlb_window->write_block(l1_dest - tlb_window->get_base_address(), src, size);
    } else {
        std::lock_guard<std::mutex> lock(wc_tlb_lock);
        get_cached_wc_tlb_window()->write_block_reconfigure(
            src, translated_core, l1_dest, size, get_selected_noc_id(), tlb_data::Relaxed);
    }
}

void LocalChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    log_trace(
        LogUMD,
        "Chip::read_from_device from {} device {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_src,
        size);

    tt_xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device(dest, translated_core, l1_src, size);
        return;
    }
    if (tlb_manager_->is_tlb_mapped(translated_core, l1_src, size)) {
        TlbWindow* tlb_window = tlb_manager_->get_tlb_window(translated_core);
        tlb_window->read_block(l1_src - tlb_window->get_base_address(), dest, size);
    } else {
        std::lock_guard<std::mutex> lock(wc_tlb_lock);
        get_cached_wc_tlb_window()->read_block_reconfigure(
            dest, translated_core, l1_src, size, get_selected_noc_id(), tlb_data::Relaxed);
    }
}

void LocalChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    tt_device_->dma_write_to_device(src, size, core, addr);
}

void LocalChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    tt_device_->dma_read_from_device(dst, size, core, addr);
}

void LocalChip::dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    tt_device_->dma_multicast_write(src, size, core_start, core_end, addr);
}

void LocalChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Size must be a multiple of 4 bytes.");
    }

    if (reg_dest % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Register address must be 4-byte aligned.");
    }

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->write_to_device(src, core, reg_dest, size);
        return;
    }

    std::lock_guard<std::mutex> lock(uc_tlb_lock);

    auto translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    tlb_data config{};
    config.local_offset = reg_dest;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Strict;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_uc_tlb_window();
    tlb_window->configure(config);

    tlb_window->write_register(reg_dest - tlb_window->get_base_address(), src, size);
}

void LocalChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Size must be a multiple of 4 bytes.");
    }

    if (reg_src % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, "Register address must be 4-byte aligned.");
    }

    auto translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device(dest, translated_core, reg_src, size);
        return;
    }

    std::lock_guard<std::mutex> lock(uc_tlb_lock);

    tlb_data config{};
    config.local_offset = reg_src;
    config.x_end = translated_core.x;
    config.y_end = translated_core.y;
    config.noc_sel = is_selected_noc1() ? 1 : 0;
    config.ordering = tlb_data::Strict;
    config.static_vc = get_tt_device()->get_architecture_implementation()->get_static_vc();
    TlbWindow* tlb_window = get_cached_uc_tlb_window();
    tlb_window->configure(config);

    tlb_window->read_register(reg_src - tlb_window->get_base_address(), dest, size);
}

void LocalChip::wait_for_non_mmio_flush() {}

void LocalChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>&) {}

void LocalChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>&) {}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(const std::string& mutex_name, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_name, pci_device_id);
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(MutexType mutex_type, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_type, pci_device_id);
}

void LocalChip::init_pcie_iatus() {
    ZoneScopedC(tracy::Color::DarkGreen);
    // TODO: this should go away soon; KMD knows how to do this at page pinning time.
    for (size_t channel = 0; channel < sysmem_manager_->get_num_host_mem_channels(); channel++) {
        HugepageMapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
        size_t region_size = hugepage_map.mapping_size;

        if (!hugepage_map.mapping) {
            UMD_THROW(error::RuntimeError, fmt::format("Hugepages are not allocated for channel: {}", channel));
        }

        if (get_soc_descriptor().arch == tt::ARCH::WORMHOLE_B0) {
            // TODO: stop doing this.  The intent was good, but it's not
            // documented and nothing takes advantage of it.
            if (channel == 3) {
                region_size = HUGEPAGE_CHANNEL_3_SIZE_LIMIT;
            }
        }
        tt_device_->configure_iatu_region(channel, hugepage_map.physical_address, region_size);
    }
}

void LocalChip::set_membar_flag(
    const std::vector<CoreCoord>& cores, const uint32_t barrier_value, const uint32_t barrier_addr) {
    tt_driver_atomics::sfence();  // Ensure that writes before this do not get reordered
    std::unordered_set<CoreCoord> cores_synced = {};
    std::vector<uint32_t> barrier_val_vec = {barrier_value};
    for (const auto& core : cores) {
        write_to_device(core, barrier_val_vec.data(), barrier_addr, barrier_val_vec.size() * sizeof(uint32_t));
    }
    tt_driver_atomics::sfence();  // Ensure that all writes in the Host WC buffer are flushed
    while (cores_synced.size() != cores.size()) {
        for (const auto& core : cores) {
            if (cores_synced.find(core) == cores_synced.end()) {
                uint32_t readback_val;
                read_from_device(core, &readback_val, barrier_addr, sizeof(std::uint32_t));
                if (readback_val == barrier_value) {
                    cores_synced.insert(core);
                } else {
                    log_trace(
                        LogUMD,
                        "Waiting for core {} to recieve mem bar flag {} in function",
                        core.str(),
                        barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned.
    tt_driver_atomics::mfence();
}

void LocalChip::insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr) {
    // Ensure that this memory barrier is atomic across processes/threads.
    auto lock = lock_manager_.acquire_mutex(MutexType::MEM_BARRIER, tt_device_->get_pci_device()->get_device_num());
    set_membar_flag(cores, MemBarFlag::SET, barrier_addr);
    set_membar_flag(cores, MemBarFlag::RESET, barrier_addr);
}

void LocalChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {
    const bool include_dram_in_l1_membar = get_soc_descriptor().arch == tt::ARCH::BLACKHOLE;
    if (!cores.empty()) {
        // Insert barrier on specific cores with L1.
        std::vector<CoreCoord> workers_to_sync = {};
        std::vector<CoreCoord> eth_to_sync = {};
        std::vector<CoreCoord> dram_to_sync = {};

        for (const auto& core : cores) {
            auto core_from_soc = get_soc_descriptor().get_coord_at(core, core.coord_system);
            if (core_from_soc.core_type == CoreType::TENSIX) {
                workers_to_sync.push_back(core);
            } else if (core_from_soc.core_type == CoreType::ETH) {
                eth_to_sync.push_back(core);
            } else if (include_dram_in_l1_membar && core_from_soc.core_type == CoreType::DRAM) {
                dram_to_sync.push_back(core);
            } else {
                UMD_THROW(error::RuntimeError, "Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
            }
        }
        insert_host_to_device_barrier(workers_to_sync, l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(eth_to_sync, l1_address_params.eth_l1_barrier_base);
        if (include_dram_in_l1_membar) {
            insert_host_to_device_barrier(dram_to_sync, dram_address_params.DRAM_BARRIER_BASE);
        }
    } else {
        // Insert barrier on all cores with L1.
        insert_host_to_device_barrier(
            get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED),
            l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(
            get_soc_descriptor().get_cores(CoreType::ETH, CoordSystem::TRANSLATED),
            l1_address_params.eth_l1_barrier_base);
        if (include_dram_in_l1_membar) {
            insert_host_to_device_barrier(
                get_soc_descriptor().get_cores(CoreType::DRAM, CoordSystem::TRANSLATED),
                dram_address_params.DRAM_BARRIER_BASE);
        }
    }
}

void LocalChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {
    if (!cores.empty()) {
        for (const auto& core : cores) {
            UMD_ASSERT(
                get_soc_descriptor().get_coord_at(core, core.coord_system).core_type == CoreType::DRAM,
                error::RuntimeError,
                "Can only insert a DRAM Memory barrier on DRAM cores.");
        }
        std::vector<CoreCoord> dram_cores_vector = std::vector<CoreCoord>(cores.begin(), cores.end());
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    } else {
        // Insert Barrier on all DRAM Cores.
        std::vector<CoreCoord> dram_cores_vector = {};
        dram_cores_vector.reserve(get_soc_descriptor().get_num_dram_channels());
        for (std::uint32_t dram_idx = 0; dram_idx < get_soc_descriptor().get_num_dram_channels(); dram_idx++) {
            dram_cores_vector.push_back(
                get_soc_descriptor().get_dram_core_for_channel(dram_idx, 0, CoordSystem::TRANSLATED));
        }
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    }
}

void LocalChip::dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel) {
    std::unordered_set<CoreCoord> dram_cores_to_sync = {};
    for (const auto& chan : channels) {
        dram_cores_to_sync.insert(
            get_soc_descriptor().get_dram_core_for_channel(chan, subchannel, CoordSystem::TRANSLATED));
    }
    dram_membar(dram_cores_to_sync);
}

void LocalChip::deassert_risc_resets() {
    ZoneScopedC(tracy::Color::DarkGreen);
    if (get_soc_descriptor().arch != tt::ARCH::BLACKHOLE) {
        arc_msg(
            wormhole::ARC_MSG_COMMON_PREFIX |
                tt_device_->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(),
            true,
            {0, 0});
    }
}

int LocalChip::get_clock() { return tt_device_->get_clock(); }

int LocalChip::get_numa_node() { return tt_device_->get_pci_device()->get_numa_node(); }

TlbWindow* LocalChip::get_cached_wc_tlb_window() {
    if (cached_wc_tlb_window == nullptr) {
        cached_wc_tlb_window = std::make_unique<SiliconTlbWindow>(get_tt_device()->get_pci_device()->allocate_tlb(
            get_tt_device()->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::WC));
        return cached_wc_tlb_window.get();
    }

    return cached_wc_tlb_window.get();
}

TlbWindow* LocalChip::get_cached_uc_tlb_window() {
    if (cached_uc_tlb_window == nullptr) {
        cached_uc_tlb_window = std::make_unique<SiliconTlbWindow>(get_tt_device()->get_pci_device()->allocate_tlb(
            get_tt_device()->get_architecture_implementation()->get_cached_tlb_size(), TlbMapping::UC));
        return cached_uc_tlb_window.get();
    }

    return cached_uc_tlb_window.get();
}

void LocalChip::noc_multicast_write(
    const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    // TODO: Support other core types once needed.
    if (core_start.core_type != CoreType::TENSIX || core_end.core_type != CoreType::TENSIX) {
        UMD_THROW(error::RuntimeError, "noc_multicast_write is only supported for Tensix cores.");
    }
    tt_device_->noc_multicast_write(src, size, core_start, core_end, addr);
}
}  // namespace tt::umd
