/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>

#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/simulation_host.hpp"

namespace tt::umd {

// RTL simulation implementation using subprocess and flatbuffer communication.
class RtlSimulationChip : public SimulationChip {
public:
    RtlSimulationChip(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id);
    ~RtlSimulationChip() override = default;

    void start_device() override;
    void close_device() override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) override;
    void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) override;
    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

private:
    SimulationHost host;
};

}  // namespace tt::umd
