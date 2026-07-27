// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
class SimulationClient;
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

    // Builds a client-mode device that attaches to a live host and sources its SoC descriptor from
    // the host over the socket (see the .cpp); discovery uses this when a host already owns the
    // socket.
    static std::unique_ptr<RtlSimulationTTDevice> create_client(
        ChipId chip_id, std::unique_ptr<SimulationClient> client);

    void wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;
    std::chrono::milliseconds wait_eth_core_training(
        CoreCoord eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;
    EthTrainingStatus read_eth_core_training_status(CoreCoord eth_core) override;

    void assert_risc_reset(CoreCoord core, const RiscType selected_riscs) override;
    void deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) override;

    RtlSimCommunicator* get_communicator() { return communicator_.get(); }

protected:
    SimulationBackendType backend_type() const override { return SimulationBackendType::Rtl; }

    std::unique_ptr<TlbWindow> create_tlb_window(
        int tlb_index, size_t size, TlbMapping mapping, tlb_data config) override;
    void tile_read_bytes(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) override;
    void tile_write_bytes(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) override;
    bool handle_special_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) override;
    bool handle_special_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size) override;

private:
    // System NOC (SMN) fast path (Quasar only). `core` is a TRANSLATED coordinate; returns true when
    // the access was routed over the system NOC. These back handle_special_read/write and can grow to
    // dispatch additional special cases later.
    bool smn_read(void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size);
    bool smn_write(const void* mem_ptr, tt_xy_pair core, uint64_t addr, size_t size);

    // Client-mode constructor: this device does not own a simulator; it forwards device operations
    // to a remote host through client. Reached only via create_client(), which validates the
    // arguments before construction.
    RtlSimulationTTDevice(
        const SocDescriptor& soc_descriptor, ChipId chip_id, std::unique_ptr<SimulationClient> client);

    // Host-mode backend bring-up (communicator init, sysmem callbacks, TLB setup). Takes the
    // host-mem channel count because the RAM callbacks need it.
    void initialize_backend(int num_host_mem_channels);

    // setup_ runs at construction, teardown_ at destruction -- the one real host-vs-client
    // difference today: host mode drives the in-process RTL backend (communicator_), client mode
    // drives the remote host (attach_client()/detach_client()). Note client_ is owned by the
    // SimulationTTDevice base (pulled up there), not declared in this class.
    std::function<void()> setup_;
    std::function<void()> teardown_;

    std::unique_ptr<RtlSimCommunicator> communicator_;
};
}  // namespace tt::umd
