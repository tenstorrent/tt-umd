// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "umd/device/simulation/sim_backend.hpp"

namespace tt::umd {

/**
 * Concrete ISimBackend wrapping TODAY's libttsim.so (the 11-symbol C ABI shared by both
 * tenstorrent/ttsim-private and tenstorrent/craq-sim).
 *
 * Responsibilities:
 *   - dlopen the shared library (optionally from a sealed memfd copy),
 *   - resolve symbols via dlsym,
 *   - compute its own capability -> resolved? map from a declarative CapabilityReq table,
 *   - translate logical ISimBackend ops onto the resolved function pointers.
 *
 * It NEVER exposes raw symbol names upward; UMD sees only ISimBackend.
 */
class LibTTSimBackend final : public ISimBackend {
public:
    LibTTSimBackend(const std::filesystem::path &simulator_path, bool copy_sim_binary);
    ~LibTTSimBackend() override;

    /**
     * Minimum symbol set this backend needs to be a candidate during selection-by-probing.
     * Exposed statically so the registry can probe without constructing the backend.
     */
    static const std::vector<const char *> &minimum_required_symbols();

    // ---- ISimBackend ----
    void init() override;
    void shutdown() override;
    void tile_read(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) override;
    void tile_write(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) override;
    void pci_mem_read(uint64_t paddr, void *data, uint32_t size) override;
    void pci_mem_write(uint64_t paddr, const void *data, uint32_t size) override;
    uint32_t pci_config_read32(uint32_t bus_device_function, uint32_t offset) override;
    void advance_clock(uint32_t n_clocks) override;
    void set_dma_callbacks(
        std::function<void(uint64_t, void *, uint32_t)> rd,
        std::function<void(uint64_t, const void *, uint32_t)> wr) override;
    bool supports(SimCapability cap) const override;
    // setup_multichip / dram_fast_access intentionally inherit the unsupported defaults: the
    // symbols they would need (libttsim_create_device_by_id, ...) are absent from today's .so.

private:
    // ---- Library management (moved out of the old TTSimCommunicator) ----
    void load_library();
    void create_memfd_copy();
    off_t resize_memfd(int src_fd);
    void copy_into_memfd();
    void seal_memfd();
    void close_memfd();

    // Resolve every symbol the ABI defines, then build the capability map from the
    // CapabilityReq table.
    void resolve_symbols_and_capabilities();

    // Look up an already-resolved function pointer (asserts the capability gating it is present).
    template <typename Fn>
    Fn fn(const char *name) const;

    std::filesystem::path simulator_path_;
    bool copy_sim_binary_ = false;
    void *handle_ = nullptr;
    int memfd_ = -1;

    // name -> resolved pointer (nullptr never stored; absent == unresolved).
    std::unordered_map<std::string, void *> symbols_;
    // capability -> all-resolved?
    std::unordered_map<SimCapability, bool> capabilities_;

    // DMA callback plumbing (C trampolines -> std::function members).
    std::function<void(uint64_t, void *, uint32_t)> dma_rd_;
    std::function<void(uint64_t, const void *, uint32_t)> dma_wr_;
    static LibTTSimBackend *callback_instance_;
    static void dma_rd_trampoline(uint64_t paddr, void *p, uint32_t size);
    static void dma_wr_trampoline(uint64_t paddr, const void *p, uint32_t size);
};

}  // namespace tt::umd
