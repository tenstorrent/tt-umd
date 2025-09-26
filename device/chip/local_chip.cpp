/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/local_chip.hpp"

#include <memory>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/chip.hpp"
#include "umd/device/chip/pcie_connection.hpp"
#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/blackhole_eth.hpp"
#include "umd/device/types/communication_protocol.hpp"

extern bool umd_use_noc1;

namespace tt::umd {

static_assert(!std::is_abstract<LocalChip>(), "LocalChip must be non-abstract.");

// TLB size for DRAM on blackhole - 4GB
const uint64_t BH_4GB_TLB_SIZE = 4ULL * 1024 * 1024 * 1024;

std::unique_ptr<LocalChip> LocalChip::create(
    int physical_device_id, std::string sdesc_path, int num_host_mem_channels, IODeviceType device_type) {
    // Create TTDevice and make sure the arc is ready so we can read its telemetry.
    auto tt_device = TTDevice::create(physical_device_id, device_type);
    tt_device->init_tt_device();

    SocDescriptor soc_descriptor;
    if (sdesc_path.empty()) {
        // In case soc descriptor yaml wasn't passed, we create soc descriptor with default values for the architecture.
        soc_descriptor = SocDescriptor(tt_device->get_arch(), tt_device->get_chip_info());
    } else {
        soc_descriptor = SocDescriptor(sdesc_path, tt_device->get_chip_info());
    }

    return std::unique_ptr<LocalChip>(new LocalChip(soc_descriptor, std::move(tt_device), num_host_mem_channels));
}

std::unique_ptr<LocalChip> LocalChip::create(
    int physical_device_id, SocDescriptor soc_descriptor, int num_host_mem_channels, IODeviceType device_type) {
    // Create TTDevice and make sure the arc is ready so we can read its telemetry.
    // physical_device_id is not actually physical for JTAG devices here.
    // It represents the index within a vector of jlink devices discovered by JtagDevice.
    auto tt_device = TTDevice::create(physical_device_id, device_type);
    tt_device->init_tt_device();

    std::unique_ptr<SysmemManager> sysmem_manager = nullptr;
    std::unique_ptr<RemoteCommunication> remote_communication = nullptr;

    return std::unique_ptr<LocalChip>(new LocalChip(soc_descriptor, std::move(tt_device), num_host_mem_channels));
}

LocalChip::LocalChip(tt_SocDescriptor soc_descriptor, std::unique_ptr<TTDevice> tt_device, int num_host_mem_channels) :
    Chip(tt_device->get_chip_info(), soc_descriptor),
    tt_device_(std::move(tt_device)),
    chip_connection_(std::make_unique<PCIeConnection>(tt_device.get(), num_host_mem_channels)) {
    chip_connection_->pre_initialization_hook();
    wait_chip_to_be_ready();
    chip_connection_->post_initialization_hook();
}

LocalChip::~LocalChip() {}

void LocalChip::initialize_membars() {
    set_membar_flag(
        soc_descriptor_.get_cores(CoreType::TENSIX, CoordSystem::VIRTUAL),
        MemBarFlag::RESET,
        l1_address_params.tensix_l1_barrier_base);
    set_membar_flag(
        soc_descriptor_.get_cores(CoreType::ETH, CoordSystem::VIRTUAL),
        MemBarFlag::RESET,
        l1_address_params.eth_l1_barrier_base);

    std::vector<CoreCoord> dram_cores_vector = {};
    for (std::uint32_t dram_idx = 0; dram_idx < soc_descriptor_.get_num_dram_channels(); dram_idx++) {
        dram_cores_vector.push_back(soc_descriptor_.get_dram_core_for_channel(dram_idx, 0, CoordSystem::VIRTUAL));
    }
    set_membar_flag(dram_cores_vector, MemBarFlag::RESET, dram_address_params.DRAM_BARRIER_BASE);
}

TTDevice* LocalChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* LocalChip::get_sysmem_manager() {
    if (tt_device_->get_communication_device_type() == IODeviceType::PCIe) {
        return dynamic_cast<PCIeConnection*>(chip_connection_.get())->get_sysmem_manager();
    }
    return nullptr;
}

TLBManager* LocalChip::get_tlb_manager() {
    if (tt_device_->get_communication_device_type() == IODeviceType::PCIe) {
        return dynamic_cast<PCIeConnection*>(chip_connection_.get())->get_tlb_manager();
    }
    return nullptr;
}

void LocalChip::verify_initialization(){

};

bool LocalChip::is_mmio_capable() const { return true; }

void LocalChip::start_device() {
    chip_connection_->start_connection();
    initialize_membars();
}

void LocalChip::close_device() {
    chip_connection_->stop_connection();
    chip_started_lock_.reset();
};

void LocalChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    log_trace(
        LogSiliconDriver,
        "Chip::write_to_device to {} dev {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_dest,
        size);

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    chip_connection_->write_to_device(translated_core, src, l1_dest, size);
}

void LocalChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    log_trace(
        LogSiliconDriver,
        "Chip::read_from_device from {} device {} core {} at 0x{:x} size: {}",
        DeviceTypeToString.at(tt_device_->get_communication_device_type()),
        tt_device_->get_communication_device_id(),
        core.str(),
        l1_src,
        size);

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    chip_connection_->read_from_device(translated_core, dest, l1_src, size);
}

void LocalChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Size must be a multiple of 4 bytes");
    }

    if (reg_dest % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Register address must be 4-byte aligned");
    }

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    chip_connection_->write_to_device_reg(translated_core, src, reg_dest, size);
}

void LocalChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    if (size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Size must be a multiple of 4 bytes");
    }

    if (reg_src % sizeof(uint32_t) != 0) {
        throw std::runtime_error("Register address must be 4-byte aligned");
    }

    if (tt_device_->get_communication_device_type() != IODeviceType::PCIe) {
        tt_device_->read_from_device(dest, core, reg_src, size);
        return;
    }

    tt_xy_pair translated_core = translate_chip_coord_to_translated(core);

    chip_connection_->read_from_device_reg(translated_core, dest, reg_src, size);
}

void LocalChip::ethernet_broadcast_write(
    const void* src, uint64_t core_dest, uint32_t size, std::vector<int> broadcast_header) {
    // target_chip and target_core are ignored when broadcast is enabled.
    chip_connection_->ethernet_broadcast_write(src, core_dest, size, broadcast_header);
}

void LocalChip::wait_for_non_mmio_flush() {
    // This is a local chip, so no need to flush remote communication.
}

void LocalChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {
    // Set cores to be used by the broadcast communication.
    chip_connection_->set_remote_transfer_ethernet_cores(
        get_soc_descriptor().translate_coords_to_xy_pair(cores, CoordSystem::TRANSLATED));
}

void LocalChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {
    // Set cores to be used by the broadcast communication.
    chip_connection_->set_remote_transfer_ethernet_cores(
        get_soc_descriptor().get_eth_xy_pairs_for_channels(channels, CoordSystem::TRANSLATED));
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(std::string mutex_name, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_name, pci_device_id);
}

std::unique_lock<RobustMutex> LocalChip::acquire_mutex(MutexType mutex_type, int pci_device_id) {
    return lock_manager_.acquire_mutex(mutex_type, pci_device_id);
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
                        LogSiliconDriver,
                        "Waiting for core {} to recieve mem bar flag {} in function",
                        core.str(),
                        barrier_value);
                }
            }
        }
    }
    // Ensure that reads or writes after this do not get reordered.
    // Reordering can cause races where data gets transferred before the barrier has returned
    tt_driver_atomics::mfence();
}

void LocalChip::insert_host_to_device_barrier(const std::vector<CoreCoord>& cores, const uint32_t barrier_addr) {
    // Ensure that this memory barrier is atomic across processes/threads
    auto lock = lock_manager_.acquire_mutex(MutexType::MEM_BARRIER, tt_device_->get_pci_device()->get_device_num());
    set_membar_flag(cores, MemBarFlag::SET, barrier_addr);
    set_membar_flag(cores, MemBarFlag::RESET, barrier_addr);
}

void LocalChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {
    if (cores.size()) {
        // Insert barrier on specific cores with L1
        std::vector<CoreCoord> workers_to_sync = {};
        std::vector<CoreCoord> eth_to_sync = {};

        for (const auto& core : cores) {
            auto core_from_soc = soc_descriptor_.get_coord_at(core, core.coord_system);
            if (core_from_soc.core_type == CoreType::TENSIX) {
                workers_to_sync.push_back(core);
            } else if (core_from_soc.core_type == CoreType::ETH) {
                eth_to_sync.push_back(core);
            } else {
                TT_THROW("Can only insert an L1 Memory barrier on Tensix or Ethernet cores.");
            }
        }
        insert_host_to_device_barrier(workers_to_sync, l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(eth_to_sync, l1_address_params.eth_l1_barrier_base);
    } else {
        // Insert barrier on all cores with L1
        insert_host_to_device_barrier(
            soc_descriptor_.get_cores(CoreType::TENSIX, CoordSystem::VIRTUAL),
            l1_address_params.tensix_l1_barrier_base);
        insert_host_to_device_barrier(
            soc_descriptor_.get_cores(CoreType::ETH, CoordSystem::VIRTUAL), l1_address_params.eth_l1_barrier_base);
    }
}

void LocalChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {
    if (cores.size()) {
        for (const auto& core : cores) {
            TT_ASSERT(
                soc_descriptor_.get_coord_at(core, core.coord_system).core_type == CoreType::DRAM,
                "Can only insert a DRAM Memory barrier on DRAM cores.");
        }
        std::vector<CoreCoord> dram_cores_vector = std::vector<CoreCoord>(cores.begin(), cores.end());
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    } else {
        // Insert Barrier on all DRAM Cores
        std::vector<CoreCoord> dram_cores_vector = {};
        for (std::uint32_t dram_idx = 0; dram_idx < soc_descriptor_.get_num_dram_channels(); dram_idx++) {
            dram_cores_vector.push_back(soc_descriptor_.get_dram_core_for_channel(dram_idx, 0, CoordSystem::VIRTUAL));
        }
        insert_host_to_device_barrier(dram_cores_vector, dram_address_params.DRAM_BARRIER_BASE);
    }
}

void LocalChip::dram_membar(const std::unordered_set<uint32_t>& channels) {
    std::unordered_set<CoreCoord> dram_cores_to_sync = {};
    for (const auto& chan : channels) {
        dram_cores_to_sync.insert(soc_descriptor_.get_dram_core_for_channel(chan, 0, CoordSystem::VIRTUAL));
    }
    dram_membar(dram_cores_to_sync);
}

void LocalChip::deassert_risc_resets() {
    if (soc_descriptor_.arch != tt::ARCH::BLACKHOLE) {
        arc_msg(
            wormhole::ARC_MSG_COMMON_PREFIX |
                tt_device_->get_architecture_implementation()->get_arc_message_deassert_riscv_reset(),
            true,
            0,
            0);
    }
}

void LocalChip::set_power_state(DevicePowerState state) {
    int exit_code = 0;
    if (soc_descriptor_.arch == tt::ARCH::WORMHOLE_B0) {
        uint32_t msg = get_power_state_arc_msg(state);
        exit_code = arc_msg(wormhole::ARC_MSG_COMMON_PREFIX | msg, true, 0, 0);
    } else if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE) {
        if (state == DevicePowerState::BUSY) {
            exit_code =
                tt_device_->get_arc_messenger()->send_message((uint32_t)blackhole::ArcMessageType::AICLK_GO_BUSY);
        } else {
            exit_code =
                tt_device_->get_arc_messenger()->send_message((uint32_t)blackhole::ArcMessageType::AICLK_GO_LONG_IDLE);
        }
    }
    TT_ASSERT(exit_code == 0, "Failed to set power state to {} with exit code: {}", (int)state, exit_code);

    wait_for_aiclk_value(tt_device_.get(), state);
}

int LocalChip::get_clock() { return tt_device_->get_clock(); }

int LocalChip::get_numa_node() { return tt_device_->get_pci_device()->get_numa_node(); }
}  // namespace tt::umd
