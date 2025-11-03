/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/simulation_chip.hpp"

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/simulation/rtl_simulation_chip.hpp"
#include "umd/device/simulation/tt_sim_chip.hpp"
#include "utils.hpp"

namespace tt::umd {

std::unique_ptr<SimulationChip> SimulationChip::create(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id) {
    if (simulator_directory.extension() == ".so") {
        return std::make_unique<TTSimChip>(simulator_directory, soc_descriptor, chip_id);
    } else {
        return std::make_unique<RtlSimulationChip>(simulator_directory, soc_descriptor, chip_id);
    }
}

std::string SimulationChip::get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "soc_descriptor.yaml")
                                                 : (simulator_path / "soc_descriptor.yaml");
}

SimulationChip::SimulationChip(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id) :
    Chip(soc_descriptor), simulator_directory_(simulator_directory) {
    soc_descriptor_per_chip.emplace(chip_id, soc_descriptor);
    arch_name = soc_descriptor.arch;
    target_devices_in_cluster = {chip_id};

    if (!std::filesystem::exists(simulator_directory_)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory_);
    }
}

// Base class implementations (common simple methods).
void SimulationChip::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset(tt_xy_pair(soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED)), soft_resets);
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

void SimulationChip::wait_for_non_mmio_flush() {}

void SimulationChip::l1_membar(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::dram_membar(const std::unordered_set<uint32_t>& channels) {}

void SimulationChip::dram_membar(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::deassert_risc_resets() {}

void SimulationChip::set_power_state(DevicePowerState state) {}

int SimulationChip::get_clock() { return 0; }

int SimulationChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    const std::chrono::milliseconds timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    *return_3 = 1;
    return 0;
}

int SimulationChip::get_num_host_channels() { return 0; }

int SimulationChip::get_host_channel_size(std::uint32_t channel) {
    throw std::runtime_error("There are no host channels available.");
}

void SimulationChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    throw std::runtime_error("SimulationChip::write_to_sysmem is not available for this chip.");
}

void SimulationChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    throw std::runtime_error("SimulationChip::read_from_sysmem is not available for this chip.");
}

int SimulationChip::get_numa_node() {
    throw std::runtime_error("SimulationChip::get_numa_node is not available for this chip.");
}

TTDevice* SimulationChip::get_tt_device() {
    throw std::runtime_error("SimulationChip::get_tt_device is not available for this chip.");
}

SysmemManager* SimulationChip::get_sysmem_manager() {
    throw std::runtime_error("SimulationChip::get_sysmem_manager is not available for this chip.");
}

TLBManager* SimulationChip::get_tlb_manager() {
    throw std::runtime_error("SimulationChip::get_tlb_manager is not available for this chip.");
}

void SimulationChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {}

}  // namespace tt::umd
