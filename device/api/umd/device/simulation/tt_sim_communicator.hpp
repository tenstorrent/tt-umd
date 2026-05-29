// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

#include "umd/device/simulation/sim_backend.hpp"

namespace tt::umd {

class ISimBackend;

/**
 * TTSimCommunicator is a thin, thread-safe facade over an ISimBackend.
 *
 * It no longer touches dlopen/dlsym or any raw simulator symbol names: at construction it asks
 * the backend registry to select+build the ISimBackend whose required symbol set resolves in
 * the given simulator library, then simply forwards logical operations under a lock. This keeps
 * existing call sites (TTSimTTDevice, TTSimTlbHandle, TTSimTlbWindow) working unchanged while
 * the simulator-specific seam lives entirely behind ISimBackend.
 *
 * This class can be used independently of TTSimTTDevice for direct simulator communication.
 */
class TTSimCommunicator final {
public:
    /**
     * @param simulator_directory Path to the simulator binary/directory
     * @param copy_sim_binary If true, copy the simulator binary to a sealed memfd before loading
     */
    TTSimCommunicator(const std::filesystem::path &simulator_directory, bool copy_sim_binary = false);

    ~TTSimCommunicator();

    /**
     * Select+load the backend for the simulator library. Must be called before any other method.
     */
    void initialize();

    /** Shutdown the simulator. */
    void shutdown();

    void tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size);
    void tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size);

    void pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size);
    void pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size);

    uint32_t pci_config_read32(uint32_t bus_device_function, uint32_t offset);

    void advance_clock(uint32_t n_clocks);

    /**
     * Set callbacks for PCIe DMA memory operations, called by the simulator when the device
     * performs NOC reads/writes to the PCIe core.
     */
    void set_pcie_dma_mem_callbacks(
        std::function<void(uint64_t, void *, uint32_t)> pfn_pci_dma_mem_rd_bytes,
        std::function<void(uint64_t, const void *, uint32_t)> pfn_pci_dma_mem_wr_bytes);

    void start_sim();

    /**
     * Capability introspection passthrough. Lets call sites probe logical features (e.g.
     * SimCapability::Multichip) and fall back cleanly when the loaded backend lacks them.
     */
    bool supports(SimCapability cap) const;

    /**
     * Optional logical op: bring up multiple chips. Returns false when the backend lacks
     * SimCapability::Multichip (the case for today's libttsim.so).
     */
    bool setup_multichip(uint32_t num_chips);

private:
    // Backend selected/built by the registry at initialize() time.
    std::filesystem::path simulator_directory_;
    bool copy_sim_binary_;
    std::unique_ptr<ISimBackend> backend_;

    mutable std::mutex device_lock_;
};

}  // namespace tt::umd
