// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>

#include "umd/device/simulation/simulation_chip.hpp"

namespace tt::umd {

// TTSIM implementation using dynamic library (.so files).
class TTSimChip : public SimulationChip {
public:
    TTSimChip(
        const std::filesystem::path& simulator_directory,
        SocDescriptor soc_descriptor,
        ChipId chip_id,
        bool copy_sim_binary = false);
    ~TTSimChip() override;

    void start_device() override;
    void close_device() override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) override;
    void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) override;
    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

private:
    void create_simulator_binary();
    off_t resize_simulator_binary(int src_fd);
    void copy_simulator_binary();
    void secure_simulator_binary();
    void close_simulator_binary();
    void load_simulator_library(const std::filesystem::path& path);
    std::unique_ptr<architecture_implementation> architecture_impl_;
    int copied_simulator_fd_ = -1;

    void* libttsim_handle = nullptr;
    uint32_t libttsim_pci_device_id = 0;
    void (*pfn_libttsim_init)() = nullptr;
    void (*pfn_libttsim_exit)() = nullptr;
    uint32_t (*pfn_libttsim_pci_config_rd32)(uint32_t bus_device_function, uint32_t offset) = nullptr;
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_clock)(uint32_t n_clocks) = nullptr;
};

}  // namespace tt::umd
