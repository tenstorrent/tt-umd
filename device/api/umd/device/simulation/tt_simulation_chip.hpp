/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>

#include "umd/device/simulation/simulation_chip.hpp"

namespace tt::umd {

// TTSIM implementation using dynamic library (.so files)
class TTSimulationChip : public SimulationChip {
public:
    TTSimulationChip(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor);
    ~TTSimulationChip() override;

    void start_device() override;
    void close_device() override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;

    void send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) override;
    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

private:
    void* libttsim_handle = nullptr;
    void (*pfn_libttsim_init)() = nullptr;
    void (*pfn_libttsim_exit)() = nullptr;
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tensix_reset_deassert)(uint32_t x, uint32_t y) = nullptr;
    void (*pfn_libttsim_tensix_reset_assert)(uint32_t x, uint32_t y) = nullptr;
    void (*pfn_libttsim_clock)(uint32_t n_clocks) = nullptr;
};

}  // namespace tt::umd
