// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/simulation_chip.hpp"

#include <stdexcept>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/simulation/rtl_simulation_chip.hpp"
#include "umd/device/simulation/tt_sim_chip.hpp"
#include "utils.hpp"

namespace tt::umd {

std::unique_ptr<SimulationChip> SimulationChip::create(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id, size_t num_chips) {
    if (simulator_directory.extension() == ".so") {
        return std::make_unique<TTSimChip>(simulator_directory, soc_descriptor, chip_id, num_chips > 1);
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
    Chip(soc_descriptor), arch_name(soc_descriptor.arch), chip_id_(chip_id), simulator_directory_(simulator_directory) {
    if (!std::filesystem::exists(simulator_directory_)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory_);
    }

    sysmem_manager_ = std::make_unique<SimulationSysmemManager>(4);
}

// Base class implementations (common simple methods).
void SimulationChip::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets, bool use_noc1) {
    send_tensix_risc_reset(
        tt_xy_pair(soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED)), soft_resets, use_noc1);
}

void SimulationChip::write_to_device_reg(
    CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size, bool use_noc1) {
    write_to_device(core, src, reg_dest, size, use_noc1);
}

void SimulationChip::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size, bool use_noc1) {
    read_from_device(core, dest, reg_src, size, use_noc1);
}

void SimulationChip::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr, bool use_noc1) {
    write_to_device(core, src, addr, size, use_noc1);
}

void SimulationChip::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr, bool use_noc1) {
    read_from_device(core, dst, addr, size, use_noc1);
}

void SimulationChip::noc_multicast_write(
    void* dst, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, bool use_noc1) {
    // TODO: Support other core types once needed.
    if (core_start.core_type != CoreType::TENSIX || core_end.core_type != CoreType::TENSIX) {
        TT_THROW("noc_multicast_write is only supported for Tensix cores.");
    }
    // TODO: investigate how to do multicast in Simulation, both RTL sim and TTSim.
    // Until then, do individual writes to each core in the range.
    const tt_xy_pair translated_start = soc_descriptor_.translate_coord_to(core_start, CoordSystem::TRANSLATED);
    const tt_xy_pair translated_end = soc_descriptor_.translate_coord_to(core_end, CoordSystem::TRANSLATED);
    for (uint32_t x = translated_start.x; x <= translated_end.x; ++x) {
        for (uint32_t y = translated_start.y; y <= translated_end.y; ++y) {
            // Since we are doing set of unicasts, we must skip cores that are not actual Tensix cores.
            // These are in columns where x = 8 (ARC core, L2CPU) and x = 9 (GDDR).
            // TODO: investigate proper multicast support for simulations so we can remove this workaround.
            if (soc_descriptor_.arch == tt::ARCH::BLACKHOLE && (x == 8 || x == 9)) {
                continue;
            }
            write_to_device(CoreCoord(x, y, core_start.core_type, core_start.coord_system), dst, addr, size, use_noc1);
        }
    }
}

void SimulationChip::wait_for_non_mmio_flush(bool use_noc1) {}

void SimulationChip::l1_membar(const std::unordered_set<CoreCoord>& cores, bool use_noc1) {}

void SimulationChip::dram_membar(const std::unordered_set<uint32_t>& channels, bool use_noc1) {}

void SimulationChip::dram_membar(const std::unordered_set<CoreCoord>& cores, bool use_noc1) {}

void SimulationChip::deassert_risc_resets(bool use_noc1) {}

void SimulationChip::set_power_state(DevicePowerState state, bool use_noc1) {}

int SimulationChip::get_clock(bool use_noc1) { return 0; }

int SimulationChip::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    const std::vector<uint32_t>& args,
    const std::chrono::milliseconds timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4,
    bool use_noc1) {
    *return_3 = 1;
    return 0;
}

int SimulationChip::get_num_host_channels() { return get_sysmem_manager()->get_num_host_mem_channels(); }

int SimulationChip::get_host_channel_size(std::uint32_t channel) {
    // log_warning instead of throw because even though sysmem_manager_ may not be initialized in all cases,
    // the program should still work. It removes the need for refactoring the whole code in case
    // pcie device breaks or isn't present.
    if (!sysmem_manager_) {
        log_warning(LogUMD, "sysmem_manager was not initialized for simulation device");
        return 0;
    }

    TT_ASSERT(channel < get_num_host_channels(), "Querying size for a host channel that does not exist.");
    HugepageMapping hugepage_map = sysmem_manager_->get_hugepage_mapping(channel);
    TT_ASSERT(hugepage_map.mapping_size, "Host channel size can only be queried after the device has been started.");
    return hugepage_map.mapping_size;
}

void SimulationChip::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    get_sysmem_manager()->write_to_sysmem(channel, src, sysmem_dest, size);
}

void SimulationChip::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    get_sysmem_manager()->read_from_sysmem(channel, dest, sysmem_src, size);
}

int SimulationChip::get_numa_node() {
    throw std::runtime_error("SimulationChip::get_numa_node is not available for this chip.");
}

TTDevice* SimulationChip::get_tt_device() {
    throw std::runtime_error("SimulationChip::get_tt_device is not available for this chip.");
}

SysmemManager* SimulationChip::get_sysmem_manager() { return sysmem_manager_.get(); }

TLBManager* SimulationChip::get_tlb_manager() {
    throw std::runtime_error("SimulationChip::get_tlb_manager is not available for this chip.");
}

void SimulationChip::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void SimulationChip::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {}

}  // namespace tt::umd
