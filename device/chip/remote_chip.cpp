/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/chip/remote_chip.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/tt_device/remote_blackhole_tt_device.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<RemoteChip>(), "RemoteChip must be non-abstract.");

std::unique_ptr<RemoteChip> RemoteChip::create(
    LocalChip* local_chip,
    EthCoord target_eth_coord,
    std::set<uint32_t> remote_transfer_eth_channels,
    std::string sdesc_path) {
    auto sysmem_manager = local_chip->get_sysmem_manager();
    auto remote_communication = RemoteCommunication::create_remote_communication(
        local_chip->get_tt_device(),
        target_eth_coord,
        sysmem_manager->get_num_host_mem_channels() > 0 ? local_chip->get_sysmem_manager() : nullptr);
    remote_communication->set_remote_transfer_ethernet_cores(
        local_chip->get_soc_descriptor().get_eth_xy_pairs_for_channels(
            remote_transfer_eth_channels, CoordSystem::TRANSLATED));
    auto remote_tt_device = TTDevice::create(std::move(remote_communication));
    remote_tt_device->init_tt_device();

    SocDescriptor soc_descriptor;
    if (sdesc_path.empty()) {
        soc_descriptor = SocDescriptor(remote_tt_device->get_arch(), remote_tt_device->get_chip_info());
    } else {
        soc_descriptor = SocDescriptor(sdesc_path, remote_tt_device->get_chip_info());
    }
    return std::unique_ptr<RemoteChip>(new RemoteChip(soc_descriptor, local_chip, std::move(remote_tt_device)));
}

std::unique_ptr<RemoteChip> RemoteChip::create(
    LocalChip* local_chip,
    EthCoord target_eth_coord,
    std::set<uint32_t> remote_transfer_eth_channels,
    SocDescriptor soc_descriptor) {
    auto sysmem_manager = local_chip->get_sysmem_manager();
    auto remote_communication = RemoteCommunication::create_remote_communication(
        local_chip->get_tt_device(),
        target_eth_coord,
        sysmem_manager->get_num_host_mem_channels() > 0 ? local_chip->get_sysmem_manager() : nullptr);
    remote_communication->set_remote_transfer_ethernet_cores(
        local_chip->get_soc_descriptor().get_eth_xy_pairs_for_channels(
            remote_transfer_eth_channels, CoordSystem::TRANSLATED));
    auto remote_tt_device = TTDevice::create(std::move(remote_communication));
    remote_tt_device->init_tt_device();

    return std::unique_ptr<RemoteChip>(new RemoteChip(soc_descriptor, local_chip, std::move(remote_tt_device)));
}

RemoteChip::RemoteChip(
    SocDescriptor soc_descriptor, LocalChip* local_chip, std::unique_ptr<TTDevice> remote_tt_device) :
    Chip(remote_tt_device->get_chip_info(), soc_descriptor), local_chip_(local_chip) {
    // Architectural design issue - this dynamic_cast reveals a leaky abstraction.
    // The base TTDevice interface should provide access to RemoteCommunication directly,
    // rather than requiring knowledge of the concrete RemoteWormholeTTDevice type.
    // This violates the Liskov Substitution Principle and creates tight coupling.
    // Consider either:
    //   1. Adding get_remote_communication() to the TTDevice base interface (probably not)
    //   2. Restructuring the inheritance hierarchy to eliminate this dependency
    //   3. Using composition instead of inheritance for remote communication
    // ToDo: Figure out a proper way to make an abstraction to redesign this.
    if (local_chip->get_tt_device()->get_arch() == tt::ARCH::WORMHOLE_B0) {
        remote_communication_ =
            dynamic_cast<RemoteWormholeTTDevice*>(remote_tt_device.get())->get_remote_communication();
    } else {
        remote_communication_ =
            dynamic_cast<RemoteBlackholeTTDevice*>(remote_tt_device.get())->get_remote_communication();
    }
    tt_device_ = std::move(remote_tt_device);
    wait_chip_to_be_ready();
}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::start_device() {}

void RemoteChip::close_device() {
    // Investigating https://github.com/tenstorrent/tt-metal/issues/25377 found that closing device that was already put
    // in LONG_IDLE by tt-smi reset would hang
    if ((uint32_t)local_chip_->get_clock() != local_chip_->get_tt_device()->get_min_clock_freq()) {
        if ((uint32_t)get_clock() != get_tt_device()->get_min_clock_freq()) {
            set_power_state(DevicePowerState::LONG_IDLE);
            assert_risc_reset(RiscType::ALL);
        }
    }
}

void RemoteChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    tt_device_->write_to_device(src, translate_chip_coord_to_translated(core), l1_dest, size);
}

void RemoteChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    tt_device_->read_from_device(dest, translate_chip_coord_to_translated(core), l1_src, size);
}

void RemoteChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void RemoteChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void RemoteChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    TT_THROW("RemoteChip::dma_write_to_device is not available for this chip.");
}

void RemoteChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    TT_THROW("RemoteChip::dma_read_from_device is not available for this chip.");
}

void RemoteChip::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

void RemoteChip::l1_membar(const std::unordered_set<CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<uint32_t>& channels) { wait_for_non_mmio_flush(); }

void RemoteChip::deassert_risc_resets() { local_chip_->deassert_risc_resets(); }

int RemoteChip::get_clock() { return tt_device_->get_clock(); }

int RemoteChip::get_num_host_channels() { return 0; }

int RemoteChip::get_host_channel_size(std::uint32_t channel) { TT_THROW("There are no host channels available."); }

void RemoteChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    TT_THROW("RemoteChip::write_to_sysmem is not available for this chip.");
}

void RemoteChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    TT_THROW("RemoteChip::read_from_sysmem is not available for this chip.");
}

int RemoteChip::get_numa_node() { TT_THROW("RemoteChip::get_numa_node is not available for this chip."); }

void RemoteChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {
    remote_communication_->set_remote_transfer_ethernet_cores(
        local_chip_->get_soc_descriptor().translate_coords_to_xy_pair(cores, CoordSystem::TRANSLATED));
}

void RemoteChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {
    remote_communication_->set_remote_transfer_ethernet_cores(
        local_chip_->get_soc_descriptor().get_eth_xy_pairs_for_channels(channels, CoordSystem::TRANSLATED));
}

TTDevice* RemoteChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* RemoteChip::get_sysmem_manager() {
    TT_THROW("RemoteChip::get_sysmem_manager is not available for this chip.");
}

TLBManager* RemoteChip::get_tlb_manager() { TT_THROW("RemoteChip::get_tlb_manager is not available for this chip."); }

RemoteCommunication* RemoteChip::get_remote_communication() { return remote_communication_; }
}  // namespace tt::umd
