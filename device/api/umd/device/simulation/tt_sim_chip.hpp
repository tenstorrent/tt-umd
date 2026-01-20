// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>

#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"

namespace tt::umd {

// TTSIM implementation using dynamic library (.so files).
class TTSimChip : public SimulationChip {
public:
    TTSimChip(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        bool copy_sim_binary = false,
        int num_host_mem_channels = 0);
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

    std::unique_ptr<TTSimTTDevice> tt_device_;
};

}  // namespace tt::umd
