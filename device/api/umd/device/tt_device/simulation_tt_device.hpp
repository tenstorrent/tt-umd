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
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

class SimulationSysmemManager;
class SimulationTlbAllocator;
class SimulationServerSocket;
class TlbWindow;

// Common base class for the simulation TTDevice backends (TTSimTTDevice and RtlSimulationTTDevice).
// It is introduced as an intermediary in the class hierarchy and owns the state that is shared by
// both backends. The behavior that operates on this state still lives in the derived classes for now
// and will be migrated into this base incrementally.
//
// The backend communicator is intentionally NOT owned here: TTSimCommunicator and RtlSimCommunicator
// are unrelated `final` classes with no common base, so each derived device keeps its own
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
        void* src,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    void noc_multicast_write(
        const void* src,
        size_t size,
        tt_xy_pair core_start,
        tt_xy_pair core_end,
        uint64_t addr,
        NocId noc_id = NocId::DEFAULT_NOC) override;
    using TTDevice::noc_multicast_write;
    void noc_multicast_write(const void* src, size_t size, uint64_t addr, NocId noc_id = NocId::DEFAULT_NOC) override;

    SimulationSysmemManager* get_sysmem_manager() override { return sysmem_manager_.get(); }

    SimulationTlbAllocator* get_tlb_allocator() { return tlb_allocator_.get(); }

protected:
    SimulationTTDevice(
        const std::filesystem::path& simulator_directory, std::unique_ptr<SimulationSysmemManager> sysmem_manager);

    void retrain_dram_core(const uint32_t dram_channel) override;

    // Client-mode constructor: the device does not own a local simulator, so it has no simulator
    // directory or sysmem manager -- those live on the remote host reached over the socket.
    SimulationTTDevice() = default;

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
