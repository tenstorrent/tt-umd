// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "umd/device/types/arch.hpp"

namespace tt::umd {

/**
 * ISimBackend is the logical, simulator-agnostic surface UMD talks to. It is
 * deliberately free of any dlopen/dlsym/symbol details and of any per-simulator
 * convention. A concrete backend (e.g. TtsimBackend) owns its own device handle
 * and forwards every call to that handle, so:
 *   - there are no process-global statics,
 *   - there is no "select this device before doing I/O" step,
 *   - each device's DMA callbacks are routed by its own context pointer.
 *
 * Identity and capabilities come from the loaded .so itself (variant string,
 * ABI version, capability bitmask, self-described arch + SOC descriptor), so the
 * factory selects and validates a backend without inspecting file extensions or
 * looking for a sibling soc_descriptor.yaml.
 */
class ISimBackend {
public:
    // DMA callbacks invoked when the *device* initiates reads/writes into host
    // system memory. Each backend instance keeps its own pair, so two devices in
    // one process never overwrite each other's callbacks.
    using DmaReadFn = std::function<void(uint64_t sysmem_addr, void* dst, uint32_t size)>;
    using DmaWriteFn = std::function<void(uint64_t sysmem_addr, const void* src, uint32_t size)>;

    virtual ~ISimBackend() = default;

    // ---- Self-description (from the .so, not a sibling file) ----
    virtual std::string variant() const = 0;
    virtual uint32_t abi_version() const = 0;
    virtual uint64_t capabilities() const = 0;
    virtual bool has_capability(uint64_t cap_bit) const = 0;
    virtual tt::ARCH arch() const = 0;
    // Full SOC descriptor as YAML text, sourced from the simulator itself.
    virtual std::string soc_descriptor_yaml() const = 0;

    // ---- Per-device I/O (the handle is owned internally) ----
    virtual void tile_read(uint32_t x, uint32_t y, uint64_t addr, void* dst, uint32_t size) = 0;
    virtual void tile_write(uint32_t x, uint32_t y, uint64_t addr, const void* src, uint32_t size) = 0;
    virtual void pci_mem_read(uint64_t paddr, void* dst, uint32_t size) = 0;
    virtual void pci_mem_write(uint64_t paddr, const void* src, uint32_t size) = 0;
    virtual uint32_t pci_config_read32(uint32_t bdf, uint32_t offset) = 0;
    virtual void advance_clock(uint32_t n_clocks) = 0;

    // ---- Per-device DMA callbacks ----
    virtual void set_dma_callbacks(DmaReadFn rd, DmaWriteFn wr) = 0;
};

}  // namespace tt::umd
