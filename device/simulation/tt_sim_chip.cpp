// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/tt_sim_chip.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

namespace tt::umd {

static_assert(!std::is_abstract<TTSimChip>(), "TTSimChip must be non-abstract.");

TTSimChip::TTSimChip(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary,
    int num_host_mem_channels) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels) {
    tt_device_ = std::make_unique<TTSimTTDevice>(simulator_directory, soc_descriptor, chip_id, copy_sim_binary);
}

TTSimChip::~TTSimChip() = default;

void TTSimChip::start_device() { tt_device_->start_device(); }

void TTSimChip::close_device() { tt_device_->close_device(); }

void TTSimChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->write_to_device(src, soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), l1_dest, size);
}

void TTSimChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->read_from_device(dest, soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), l1_src, size);
}

void TTSimChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->send_tensix_risc_reset(translated_core, soft_resets);
}

void TTSimChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    tt_device_->send_tensix_risc_reset(soft_resets);
}

void TTSimChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->assert_risc_reset(soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), selected_riscs);
}

void TTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_device_->deassert_risc_reset(
        soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED), selected_riscs, staggered_start);
}

}  // namespace tt::umd
