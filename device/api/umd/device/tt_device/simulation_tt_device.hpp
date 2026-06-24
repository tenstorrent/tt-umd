// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>

#include "umd/device/chip_helpers/simulation_sysmem_manager.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class SimulationSysmemManager;
class SimulationTlbAllocator;
class SimulationServerSocket;
class TlbWindow;

// Common base class for the simulation TTDevice backends. It sits as an intermediary in the class
// hierarchy and owns the state shared by the derived simulation devices.
//
// The backend communicator is intentionally not owned here; each derived device keeps its own
// concretely-typed communicator.
class SimulationTTDevice : public TTDevice {
public:
    ~SimulationTTDevice() override;

    // Takes ownership of the serving socket that exposes this device (created by discovery).
    void adopt_socket(std::unique_ptr<SimulationServerSocket> socket);

    // --- TTDevice overrides whose behavior is identical across both simulation backends ---
    void dma_d2h(void* dst, uint32_t src, size_t size) override;
    void dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) override;
    void dma_h2d(uint32_t dst, const void* src, size_t size) override;
    void dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) override;
    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    void write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) override;
    uint32_t get_clock() override;
    uint32_t get_min_clock_freq() override;
    bool get_noc_translation_enabled() override;
    void dma_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void noc_multicast_write(
        const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;
    using TTDevice::noc_multicast_write;
    void noc_multicast_write(const void* src, size_t size, uint64_t addr) override;

    SimulationSysmemManager* get_sysmem_manager() override { return sysmem_manager_.get(); }

    SimulationTlbAllocator* get_tlb_allocator() { return tlb_allocator_.get(); }

    std::unique_ptr<TlbWindow> get_io_window(tlb_data config, TlbMapping mapping, size_t size) override;

protected:
    SimulationTTDevice(
        const std::filesystem::path& simulator_directory, std::unique_ptr<SimulationSysmemManager> sysmem_manager);

    void retrain_dram_core(const uint32_t dram_channel) override;

    // Client-mode constructor: the device does not own a local simulator, so it has no simulator
    // directory or sysmem manager -- those live on the remote host reached over the socket.
    SimulationTTDevice() = default;

    // Build tlb_allocator_ once the backend knows its BAR0 base (0 for RTL, PCI-probed for TTSim).
    void init_tlb_allocator(uint64_t bar0_base);
    // Allocate the cached default TLB window for the current arch. Must be invoked from the derived
    // constructor once its communicator exists, since it reaches the backend through the virtual
    // create_tlb_window() hook.
    void setup_cached_tlb_window();

    // Construct the backend-specific TlbHandle + TlbWindow for an already-allocated TLB index.
    virtual std::unique_ptr<TlbWindow> create_tlb_window(
        int tlb_index, size_t size, TlbMapping mapping, tlb_data config) = 0;

    std::recursive_mutex device_lock;
    std::filesystem::path simulator_directory_;
    std::unique_ptr<SimulationSysmemManager> sysmem_manager_;
    std::shared_ptr<SimulationTlbAllocator> tlb_allocator_;
    std::unique_ptr<TlbWindow> cached_tlb_window_ = nullptr;

    // Exposes this device on disk as a UNIX socket ("the card"), so other UMD clients can find it.
    // The host keeps its own direct in-process fast path; the socket is for remote clients.
    std::unique_ptr<SimulationServerSocket> socket_;
};

}  // namespace tt::umd
