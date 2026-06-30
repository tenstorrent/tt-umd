// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_chip.hpp"

#include <fmt/format.h>

#include <mutex>
#include <tt-logger/tt-logger.hpp>

#include "tracy.hpp"
#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/simulation_device_factory.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

std::unique_ptr<SimulationChip> SimulationChip::create(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    size_t num_chips,
    int num_host_mem_channels) {
    auto tt_device =
        create_simulation_tt_device(simulator_directory, soc_descriptor, chip_id, num_chips, num_host_mem_channels);
    return std::make_unique<SimulationChip>(simulator_directory, soc_descriptor, chip_id, std::move(tt_device));
}

std::string SimulationChip::get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "soc_descriptor.yaml")
                                                 : (simulator_path / "soc_descriptor.yaml");
}

std::string SimulationChip::get_cluster_descriptor_path_from_simulator_path(
    const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "cluster_descriptor.yaml")
                                                 : (simulator_path / "cluster_descriptor.yaml");
}

SimulationChip::SimulationChip(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    std::unique_ptr<TTDevice> tt_device) :
    Chip(soc_descriptor.arch),
    arch_name(soc_descriptor.arch),
    chip_id_(chip_id),
    simulator_directory_(simulator_directory),
    tt_device_(std::move(tt_device)),
    tlb_manager_(tt_device_ ? std::make_unique<TLBManager>(tt_device_.get()) : nullptr) {
    UMD_ASSERT(tt_device_ != nullptr, error::RuntimeError, "SimulationChip requires a non-null TTDevice.");
    if (!std::filesystem::exists(simulator_directory_)) {
        UMD_THROW(error::RuntimeError, fmt::format("Simulator binary not found at: {}", simulator_directory_.string()));
    }
}

void SimulationChip::start_device(uint32_t dram_membar_subchannel) {}

void SimulationChip::close_device() {}

void SimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->write_to_device(src, get_soc_descriptor().translate_chip_coord_to_translated(core), l1_dest, size);
}

void SimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->read_from_device(dest, get_soc_descriptor().translate_chip_coord_to_translated(core), l1_src, size);
}

void SimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    ZoneScopedC(tracy::Color::DarkRed);
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->assert_risc_reset(get_soc_descriptor().translate_chip_coord_to_translated(core), selected_riscs);
}

void SimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    ZoneScopedC(tracy::Color::DarkGreen);
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->deassert_risc_reset(
        get_soc_descriptor().translate_chip_coord_to_translated(core), selected_riscs, staggered_start);
}

void SimulationChip::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void SimulationChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void SimulationChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    write_to_device(core, src, addr, size);
}

void SimulationChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    read_from_device(core, dst, addr, size);
}

void SimulationChip::dma_multicast_write(
    void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "dma_multicast_write is not supported in SimulationChip.");
}

void SimulationChip::wait_for_non_mmio_flush() {}

void SimulationChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::dram_membar(const std::unordered_set<uint32_t>& channels, uint32_t subchannel) {}

void SimulationChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::deassert_risc_resets() {}

void SimulationChip::set_power_state(DevicePowerState state) {}

int SimulationChip::get_clock() { return 0; }

int SimulationChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    *return_3 = 1;
    return 0;
}

int SimulationChip::get_num_host_channels() {
    SysmemManager* mgr = get_sysmem_manager();
    if (!mgr) {
        log_warning(LogUMD, "SysmemManager was not initialized for simulation device.");
        return 0;
    }
    return mgr->get_num_host_mem_channels();
}

int SimulationChip::get_host_channel_size(std::uint32_t channel) {
    SysmemManager* mgr = get_sysmem_manager();
    if (!mgr) {
        log_warning(LogUMD, "SysmemManager was not initialized for simulation device.");
        return 0;
    }
    UMD_ASSERT(
        channel < get_num_host_channels(),
        error::RuntimeError,
        "Querying size for a host channel that does not exist.");
    HugepageMapping hugepage_map = mgr->get_hugepage_mapping(channel);
    UMD_ASSERT(
        hugepage_map.mapping_size,
        error::RuntimeError,
        "Host channel size can only be queried after the device has been started.");
    return hugepage_map.mapping_size;
}

void SimulationChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    SysmemManager* mgr = get_sysmem_manager();
    if (!mgr) {
        UMD_THROW(error::RuntimeError, "SysmemManager was not initialized for simulation device.");
    }
    mgr->write_to_sysmem(channel, src, sysmem_dest, size);
}

void SimulationChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    SysmemManager* mgr = get_sysmem_manager();
    if (!mgr) {
        UMD_THROW(error::RuntimeError, "SysmemManager was not initialized for simulation device.");
    }
    mgr->read_from_sysmem(channel, dest, sysmem_src, size);
}

int SimulationChip::get_numa_node() {
    UMD_THROW(error::RuntimeError, "SimulationChip::get_numa_node() is not available for this chip.");
}

TTDevice* SimulationChip::get_tt_device() { return tt_device_.get(); }

SysmemManager* SimulationChip::get_sysmem_manager() { return tt_device_->get_sysmem_manager(); }

TLBManager* SimulationChip::get_tlb_manager() { return tlb_manager_.get(); }

void SimulationChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {}

}  // namespace tt::umd
