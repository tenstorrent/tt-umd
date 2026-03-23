// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_simulation_chip.hpp"

#include <iostream>
#include <string>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<RtlSimulationChip>(), "RtlSimulationChip must be non-abstract.");

RtlSimulationChip::RtlSimulationChip(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id),
    tt_device_(
        std::make_unique<RtlSimulationTTDevice>(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels)) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");
}

void RtlSimulationChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->start_device();
}

void RtlSimulationChip::close_device() {}

void RtlSimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    tt_device_->write_to_device(src, translate_core, l1_dest, size);
}

void RtlSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    tt_device_->read_from_device(dest, translate_core, l1_src, size);
}

void RtlSimulationChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->send_tensix_risc_reset(translated_core, soft_resets);
}

void RtlSimulationChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset(tt_xy_pair(0, 0), soft_resets);
}

void RtlSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    tt_device_->assert_risc_reset(translate_core, selected_riscs);
}

void RtlSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    tt_device_->deassert_risc_reset(translate_core, selected_riscs, staggered_start);
}

}  // namespace tt::umd
