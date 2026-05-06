// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_simulation_chip.hpp"

#include <mutex>
#include <tt-logger/tt-logger.hpp>
#include <type_traits>

#include "tracy.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<RtlSimulationChip>(), "RtlSimulationChip must be non-abstract.");

RtlSimulationChip::RtlSimulationChip(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id),
    tt_device_(
        std::make_unique<RtlSimulationTTDevice>(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels)),
    tlb_manager_(std::make_unique<TLBManager>(tt_device_.get())) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");
}

void RtlSimulationChip::start_device(uint32_t dram_membar_subchannel) {}

void RtlSimulationChip::close_device() {}

void RtlSimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, size_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    tt_device_->write_to_device(src, translate_core, l1_dest, size);
}

void RtlSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, size_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    tt_device_->read_from_device(dest, translate_core, l1_src, size);
}

void RtlSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    ZoneScopedC(tracy::Color::DarkRed);
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    tt_device_->assert_risc_reset(translate_core, selected_riscs);
}

void RtlSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    ZoneScopedC(tracy::Color::DarkGreen);
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    tt_device_->deassert_risc_reset(translate_core, selected_riscs, staggered_start);
}

TLBManager* RtlSimulationChip::get_tlb_manager() { return tlb_manager_.get(); }

}  // namespace tt::umd
