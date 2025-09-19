/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/tt_simulation_chip.hpp"

#include <dlfcn.h>

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/driver_atomics.hpp"

#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find '%s' symbol: ", #func_name, dlerror());             \
    }

namespace tt::umd {

static_assert(!std::is_abstract<TTSimulationChip>(), "TTSimulationChip must be non-abstract.");

TTSimulationChip::TTSimulationChip(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor) :
    SimulationChip(simulator_directory, soc_descriptor) {
    std::filesystem::path simulator_path = simulator_directory_;
    if (simulator_path.extension() != ".so") {
        TT_THROW("TTSimulationChip expects a .so file, got: ", simulator_path);
    }

    // dlopen the simulator library and dlsym the entry points
    libttsim_handle = dlopen(simulator_path.string().c_str(), RTLD_LAZY);
    if (!libttsim_handle) {
        TT_THROW("Failed to dlopen simulator library: ", dlerror());
    }
    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_tensix_reset_deassert)
    DLSYM_FUNCTION(libttsim_tensix_reset_assert)
    DLSYM_FUNCTION(libttsim_clock)
}

TTSimulationChip::~TTSimulationChip() {
    if (libttsim_handle) {
        dlclose(libttsim_handle);
    }
}

void TTSimulationChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    pfn_libttsim_init();
}

void TTSimulationChip::close_device() {
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    pfn_libttsim_exit();
}

void TTSimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, l1_dest, src, size);
    pfn_libttsim_clock(10);
}

void TTSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, l1_src, dest, size);
    pfn_libttsim_clock(10);
}

void TTSimulationChip::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
        pfn_libttsim_tensix_reset_assert(translate_core.x, translate_core.y);
    } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
        pfn_libttsim_tensix_reset_deassert(translate_core.x, translate_core.y);
    } else {
        TT_THROW("Invalid soft reset option.");
    }
}

void TTSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    // TTSIM doesn't support this more granular reset control
    // Fall back to basic tensix reset
    log_debug(tt::LogEmulationDriver, "TTSIM: Falling back to basic tensix reset for risc_type {}", selected_riscs);
    send_tensix_risc_reset(core, TENSIX_ASSERT_SOFT_RESET);
}

void TTSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    // TTSIM doesn't support this more granular reset control
    // Fall back to basic tensix reset
    log_debug(tt::LogEmulationDriver, "TTSIM: Falling back to basic tensix reset for risc_type {}", selected_riscs);
    send_tensix_risc_reset(core, TENSIX_DEASSERT_SOFT_RESET);
}

}  // namespace tt::umd
