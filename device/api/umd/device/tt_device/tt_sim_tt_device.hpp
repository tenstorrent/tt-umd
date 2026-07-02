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
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/simulation/simulation_host.hpp"
#include "umd/device/simulation/tt_sim_communicator.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/simulation_tt_device.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class TTSimCommunicator;
class SimulationSysmemManager;
class SimulationServerSocket;
class SimulationClient;
class SocDescriptor;

class TTSimTTDevice : public SimulationTTDevice {
public:
    TTSimTTDevice(
        const std::filesystem::path &simulator_directory,
        const SocDescriptor &soc_descriptor,
        ChipId chip_id,
        bool copy_sim_binary = false,
        int num_host_mem_channels = 0);

    ~TTSimTTDevice();

    static std::unique_ptr<TTSimTTDevice> create(
        const std::filesystem::path &simulator_directory, int num_host_mem_channels = 0, bool copy_sim_binary = false);

    // Factory for multichip testing: create a device with an explicit chip_id
    // so callers can open two devices with distinct IDs (chip 0 and chip 1) and
    // verify that I/O on one does not affect the other.
    // Named distinctly from create() because ChipId is an alias for int, which
    // would otherwise produce a duplicate signature.
    static std::unique_ptr<TTSimTTDevice> create_for_chip(
        const std::filesystem::path &simulator_directory,
        ChipId chip_id,
        int num_host_mem_channels = 0,
        bool copy_sim_binary = false);

    // Builds a client-mode device that attaches to a live host (see the .cpp for how the SoC
    // descriptor is sourced); discovery uses this when a host already owns the socket.
    static std::unique_ptr<TTSimTTDevice> create_client(
        const std::filesystem::path &simulator_directory, ChipId chip_id, std::unique_ptr<SimulationClient> client);

    void read_from_device(void *mem_ptr, CoreCoord core, uint64_t addr, size_t size) override;
    void write_to_device(const void *mem_ptr, CoreCoord core, uint64_t addr, size_t size) override;

    void wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;
    std::chrono::milliseconds wait_eth_core_training(
        const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;
    EthTrainingStatus read_eth_core_training_status(tt_xy_pair eth_core) override;
    ChipInfo get_chip_info() override;

    void close_device();
    void start_device();

    void assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) override;
    void deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) override;

    void advance_device_execution() override;

    /**
     * Get the TTSimCommunicator for low-level device operations.
     * @return Pointer to TTSimCommunicator
     */
    TTSimCommunicator *get_communicator() { return communicator_.get(); }

    std::unique_ptr<TlbWindow> get_io_window(tlb_data config, TlbMapping mapping, size_t size) override;

    uint64_t bar0_base = 0;
    uint64_t bar4_base = 0;

private:
    // Client-mode constructor: this device does not own a simulator (.so); it forwards device
    // operations to a remote host through client. Reached only via create_client(), which
    // validates the arguments before construction.
    TTSimTTDevice(const SocDescriptor &soc_descriptor, ChipId chip_id, std::unique_ptr<SimulationClient> client);

    void initialize_sysmem_functions();
    void pci_dma_read_bytes(uint64_t paddr, void *p, uint32_t size);
    void pci_dma_write_bytes(uint64_t paddr, const void *p, uint32_t size);

    // Host-mode backend bring-up (.so init, PCI read, TLB setup).
    void initialize_backend();

    // setup_ runs at construction, teardown_ at destruction -- the one real host-vs-client
    // difference today: host mode drives the in-process .so backend (communicator_), client mode
    // drives the remote host (client_->attach()/detach()).
    std::function<void()> setup_;
    std::function<void()> teardown_;

    // Set only in client mode; the remote host this device talks to. Null in host/local mode.
    std::unique_ptr<SimulationClient> client_;

    uint32_t tlb_region_size_ = 0;
    std::unique_ptr<TTSimCommunicator> communicator_;
    ChipId chip_id_;
    uint32_t libttsim_pci_device_id;
};
}  // namespace tt::umd
