/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dlfcn.h>

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/driver_atomics.hpp"
#include "umd/device/simulation/tt_simulation_chip.hpp"

#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find '%s' symbol: ", #func_name, dlerror());             \
    }

namespace tt::umd {

static_assert(!std::is_abstract<TTSimulationChip>(), "TTSimulationChip must be non-abstract.");

TTSimulationChip::TTSimulationChip(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor) :
    SimulationChip(simulator_directory, soc_descriptor),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)) {
    if (!std::filesystem::exists(simulator_directory)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory);
    }

    // dlopen the simulator library and dlsym the entry points.
    libttsim_handle = dlopen(simulator_directory.string().c_str(), RTLD_LAZY);
    if (!libttsim_handle) {
        TT_THROW("Failed to dlopen simulator library: ", dlerror());
    }
    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_pci_config_rd32)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_clock)
}

TTSimulationChip::~TTSimulationChip() { dlclose(libttsim_handle); }

void TTSimulationChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    pfn_libttsim_init();

    // Read the PCI ID (first 32 bits of PCI config space)
    uint32_t pci_id = pfn_libttsim_pci_config_rd32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = pci_id >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");
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
}

void TTSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, l1_src, dest, size);
    pfn_libttsim_clock(10);
}

void TTSimulationChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    if ((libttsim_pci_device_id == 0x401E) || (libttsim_pci_device_id == 0xB140)) {  // WH/BH
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint32_t reset_value = uint32_t(soft_resets);
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        TT_THROW("Missing implementation of reset for this chip.");
    }
}

void TTSimulationChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    Chip::send_tensix_risc_reset(soft_resets);
}

void TTSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    uint32_t reset_value;
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    reset_value |= soft_reset_update;
    pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
}

void TTSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    uint32_t reset_value;
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    reset_value &= ~soft_reset_update;
    pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
}

}  // namespace tt::umd
