// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/simulation/rtl_sim_communicator.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/simulation_tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class RtlSimCommunicator;
class SimulationSysmemManager;
class SimulationServerSocket;
class SocDescriptor;
class TlbWindow;

class RtlSimulationTTDevice : public SimulationTTDevice {
public:
    RtlSimulationTTDevice(
        const std::filesystem::path& simulator_directory,
        const SocDescriptor& soc_descriptor,
        ChipId chip_id,
        int num_host_mem_channels = 0);
    ~RtlSimulationTTDevice();

    static std::unique_ptr<RtlSimulationTTDevice> create(
        const std::filesystem::path& simulator_directory, int num_host_mem_channels = 0);

    void wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;
    std::chrono::milliseconds wait_eth_core_training(
        const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;
    EthTrainingStatus read_eth_core_training_status(tt_xy_pair eth_core) override;

    void assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) override;
    void deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) override;

    RtlSimCommunicator* get_communicator() { return communicator_.get(); }

protected:
    std::unique_ptr<TlbWindow> create_tlb_window(
        int tlb_index, size_t size, TlbMapping mapping, tlb_data config) override;
    void tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) override;
    void tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) override;
    bool handle_special_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) override;
    bool handle_special_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) override;

private:
    std::unique_ptr<RtlSimCommunicator> communicator_;
};
}  // namespace tt::umd
