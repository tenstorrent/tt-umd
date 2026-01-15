// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_simulation_chip.hpp"

#include <nng/nng.h>
#include <uv.h>

#include <iostream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "simulation_device_generated.h"

namespace tt::umd {

static_assert(!std::is_abstract<RtlSimulationChip>(), "RtlSimulationChip must be non-abstract.");

RtlSimulationChip::RtlSimulationChip(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    int num_host_mem_channels) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels) {
    tt_device_ = std::make_unique<RtlSimulationTTDevice>(simulator_directory, soc_descriptor);
}

void RtlSimulationChip::start_device() {}

void RtlSimulationChip::close_device() { tt_device_->close_device(); }

void RtlSimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->write_to_device(src, soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), l1_dest, size);
}

void RtlSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->write_to_device(dest, soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), l1_src, size);
}

void RtlSimulationChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->send_tensix_risc_reset_options(translated_core, soft_resets);
}

void RtlSimulationChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset({0, 0}, soft_resets);
}

void RtlSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->assert_risc_reset(soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), selected_riscs);
}

void RtlSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->deassert_risc_reset(
        soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), selected_riscs, staggered_start);
}

}  // namespace tt::umd
