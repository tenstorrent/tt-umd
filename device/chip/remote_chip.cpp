// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/chip/remote_chip.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include "tracy.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#ifdef TT_UMD_BUILD_SIMULATION
#include "umd/device/simulation/simulation_chip.hpp"
#endif
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/cluster_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<RemoteChip>(), "RemoteChip must be non-abstract.");

std::unique_ptr<RemoteChip> RemoteChip::create(std::unique_ptr<TTDevice> remote_tt_device, Chip* local_chip) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(
        remote_tt_device != nullptr, error::RuntimeError, "RemoteTTDevice passed to RemoteChip must not be null.");
    UMD_ASSERT(local_chip != nullptr, error::RuntimeError, "Local chip passed to RemoteChip must not be null.");
    return std::unique_ptr<RemoteChip>(new RemoteChip(local_chip, std::move(remote_tt_device)));
}

std::unique_ptr<RemoteChip> RemoteChip::create_for_simulation(
    std::unique_ptr<TTDevice> remote_tt_device, Chip* local_chip, ChipInfo chip_info) {
    ZoneScopedC(tracy::Color::DarkGreen);
    UMD_ASSERT(
        remote_tt_device != nullptr, error::RuntimeError, "RemoteTTDevice passed to RemoteChip must not be null.");
    UMD_ASSERT(local_chip != nullptr, error::RuntimeError, "Local chip passed to RemoteChip must not be null.");
    // The remote TTDevice for a simulated chip is never run through init_tt_device() (it has no ARC to probe), so
    // its SocDescriptor is supplied to TTDevice::create() instead. get_soc_descriptor() can then keep delegating
    // to the TTDevice like every other chip.
    return std::unique_ptr<RemoteChip>(new RemoteChip(local_chip, std::move(remote_tt_device), chip_info));
}

RemoteChip::RemoteChip(Chip* local_chip, std::unique_ptr<TTDevice> remote_tt_device) :
    Chip(remote_tt_device->get_chip_info(), remote_tt_device->get_arch()), local_chip_(local_chip) {
    remote_communication_ = remote_tt_device->get_remote_communication();
    tt_device_ = std::move(remote_tt_device);
    wait_chip_to_be_ready();
}

RemoteChip::RemoteChip(Chip* local_chip, std::unique_ptr<TTDevice> remote_tt_device, ChipInfo chip_info) :
    Chip(chip_info, remote_tt_device->get_soc_descriptor().arch), local_chip_(local_chip) {
    remote_communication_ = remote_tt_device->get_remote_communication();
    tt_device_ = std::move(remote_tt_device);
}

bool RemoteChip::is_mmio_capable() const { return false; }

void RemoteChip::start_device(uint32_t dram_membar_subchannel) {}

void RemoteChip::close_device() {
    ZoneScopedC(tracy::Color::DarkRed);
#ifdef TT_UMD_BUILD_SIMULATION
    if (dynamic_cast<SimulationChip*>(local_chip_) != nullptr) {
        return;
    }
#endif
    // Investigating https://github.com/tenstorrent/tt-metal/issues/25377 found that closing device that was already put
    // in LONG_IDLE by tt-smi reset would hang
    if ((uint32_t)local_chip_->get_clock() != local_chip_->get_tt_device()->get_min_clock_freq()) {
        if ((uint32_t)get_clock() != get_tt_device()->get_min_clock_freq()) {
            set_power_state(DevicePowerState::LONG_IDLE);
            assert_risc_reset(RiscType::ALL);
        }
    }
}

void RemoteChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    tt_device_->write_to_device(src, get_soc_descriptor().translate_chip_coord_to_translated(core), l1_dest, size);
}

void RemoteChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    tt_device_->read_from_device(dest, get_soc_descriptor().translate_chip_coord_to_translated(core), l1_src, size);
}

void RemoteChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void RemoteChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void RemoteChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "RemoteChip::dma_write_to_device is not available for this chip.");
}

void RemoteChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "RemoteChip::dma_read_from_device is not available for this chip.");
}

void RemoteChip::dma_multicast_write(void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "RemoteChip::dma_multicast_write is not available for this chip.");
}

void RemoteChip::noc_multicast_write(
    const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    if (core_start.core_type != CoreType::TENSIX || core_end.core_type != CoreType::TENSIX) {
        UMD_THROW(error::RuntimeError, "noc_multicast_write is only supported for Tensix cores.");
    }
    const tt_xy_pair translated_start = get_soc_descriptor().translate_chip_coord_to_translated(core_start);
    const tt_xy_pair translated_end = get_soc_descriptor().translate_chip_coord_to_translated(core_end);
    for (const auto& core : get_soc_descriptor().get_cores(CoreType::TENSIX, CoordSystem::TRANSLATED)) {
        if (core.x < translated_start.x || core.x > translated_end.x || core.y < translated_start.y ||
            core.y > translated_end.y) {
            continue;
        }
        write_to_device(CoreCoord(core.x, core.y, CoreType::TENSIX, CoordSystem::TRANSLATED), src, addr, size);
    }
}

void RemoteChip::wait_for_non_mmio_flush() { remote_communication_->wait_for_non_mmio_flush(); }

void RemoteChip::l1_membar(const std::unordered_set<CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<CoreCoord>& cores) { wait_for_non_mmio_flush(); }

void RemoteChip::dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel) {
    wait_for_non_mmio_flush();
}

void RemoteChip::deassert_risc_resets() {
#ifdef TT_UMD_BUILD_SIMULATION
    if (dynamic_cast<SimulationChip*>(local_chip_) != nullptr) {
        return;
    }
#endif
    local_chip_->deassert_risc_resets();
}

int RemoteChip::get_clock() { return tt_device_->get_clock(); }

int RemoteChip::get_num_host_channels() { return 0; }

int RemoteChip::get_host_channel_size(std::uint32_t channel) {
    UMD_THROW(error::RuntimeError, "There are no host channels available.");
}

void RemoteChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    UMD_THROW(error::RuntimeError, "RemoteChip::write_to_sysmem is not available for this chip.");
}

void RemoteChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    UMD_THROW(error::RuntimeError, "RemoteChip::read_from_sysmem is not available for this chip.");
}

int RemoteChip::get_numa_node() {
    UMD_THROW(error::RuntimeError, "RemoteChip::get_numa_node is not available for this chip.");
}

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
    UMD_THROW(error::RuntimeError, "RemoteChip::get_sysmem_manager is not available for this chip.");
}

TLBManager* RemoteChip::get_tlb_manager() {
    UMD_THROW(error::RuntimeError, "RemoteChip::get_tlb_manager is not available for this chip.");
}

RemoteCommunication* RemoteChip::get_remote_communication() { return remote_communication_; }
}  // namespace tt::umd
