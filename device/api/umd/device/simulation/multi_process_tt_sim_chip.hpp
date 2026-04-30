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

namespace tt::umd {

class ProcessManager;
class RemoteTTSimCommunicator;

// TTSIM implementation using dynamic library (.so files) with one process per chip.
class MultiProcessTTSimChip : public SimulationChip {
public:
    MultiProcessTTSimChip(
        const std::filesystem::path& simulator_directory,
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
    bool connect_eth_links();

private:
    std::unique_ptr<ProcessManager> process_manager_;
    // Parent-side stub that forwards TLB-related pci_mem_*/pci_config_read32 calls
    // to the child via process_manager_. Lives as long as this chip.
    std::unique_ptr<RemoteTTSimCommunicator> remote_communicator_;
};

}  // namespace tt::umd
