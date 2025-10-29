/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "umd/device/simulation/simulation_chip.hpp"
#include "umd/device/simulation/process_manager.hpp"

namespace tt::umd {

// MultiProcessTTSimChip implementation using separate processes for each device
class MultiProcessTTSimChip : public SimulationChip {
public:
    MultiProcessTTSimChip(const std::filesystem::path& simulator_directory,
                         SocDescriptor soc_descriptor,
                         ClusterDescriptor* cluster_desc,
                         ChipId chip_id);
    ~MultiProcessTTSimChip() override;

    void start_device() override;
    void close_device() override;

    void write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) override;
    void read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) override;

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) override;
    void send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) override;
    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;
    bool connect_eth_sockets();

private:
    std::unique_ptr<ProcessManager> process_manager_;
    std::unique_ptr<architecture_implementation> architecture_impl_;

    // Helper methods for message construction
    void send_write_message(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size);
    void send_read_message(CoreCoord core, uint64_t l1_src, uint32_t size, void* dest);
    void send_tensix_risc_reset_message(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets);
    void send_assert_risc_reset_message(CoreCoord core, const RiscType selected_riscs);
    void send_deassert_risc_reset_message(CoreCoord core, const RiscType selected_riscs, bool staggered_start);
};

}  // namespace tt::umd
