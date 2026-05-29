// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>

namespace tt::umd {

/**
 * Logical, simulator-agnostic capabilities that UMD may depend on.
 *
 * A capability is an abstract feature ("can this backend do multi-chip?"), NOT a raw symbol
 * name. Each concrete backend maps a capability onto the concrete set of C symbols it needs
 * to resolve in order to honor it. UMD code only ever asks `supports(SimCapability::X)`.
 */
enum class SimCapability : uint8_t {
    // Baseline single-chip I/O. Always required of any usable backend.
    CoreIo,
    // PCIe memory + config space access.
    PcieAccess,
    // Host-initiated DMA callbacks.
    DmaCallbacks,
    // Multi-chip orchestration. Diverges over time; ABSENT on today's libttsim.so.
    Multichip,
    // Fast direct DRAM access path (illustrative optional op).
    DramFastAccess,
};

/**
 * Declarative entry tying one capability to the exact set of C functions a backend must
 * resolve to claim it. All-or-nothing: a capability is present iff EVERY listed function
 * resolves. This prevents a "half-wired" capability where some entry points exist and some
 * do not.
 */
struct CapabilityReq {
    SimCapability cap;
    std::initializer_list<const char *> required_fns;
};

/**
 * ISimBackend is the seam between UMD and a concrete simulator shared library. It exposes
 * only logical operations; the mapping to dlopen/dlsym lives entirely in implementations.
 *
 * Optional operations default to unsupported / no-op so call sites can probe capability and
 * fall back cleanly without every backend implementing every feature.
 */
class ISimBackend {
public:
    virtual ~ISimBackend() = default;

    // ---- Lifecycle ----
    virtual void init() = 0;
    virtual void shutdown() = 0;

    // ---- Core (tile) I/O ----
    virtual void tile_read(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) = 0;
    virtual void tile_write(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) = 0;

    // ---- PCIe memory + config ----
    virtual void pci_mem_read(uint64_t paddr, void *data, uint32_t size) = 0;
    virtual void pci_mem_write(uint64_t paddr, const void *data, uint32_t size) = 0;
    virtual uint32_t pci_config_read32(uint32_t bus_device_function, uint32_t offset) = 0;

    // ---- Clock ----
    virtual void advance_clock(uint32_t n_clocks) = 0;

    // ---- DMA callbacks ----
    virtual void set_dma_callbacks(
        std::function<void(uint64_t, void *, uint32_t)> rd,
        std::function<void(uint64_t, const void *, uint32_t)> wr) = 0;

    // ---- Capability introspection ----
    virtual bool supports(SimCapability cap) const = 0;

    // ---- Optional logical ops: default to unsupported / no-op ----

    /**
     * Bring up N chips and select the active one. Requires SimCapability::Multichip.
     * Default: no-op returning false (unsupported), so callers fall back to single-chip.
     */
    virtual bool setup_multichip([[maybe_unused]] uint32_t num_chips) { return false; }

    /**
     * Fast direct DRAM access path. Returns nullopt when unsupported, signalling the caller
     * to take the regular tile/pci path instead.
     */
    virtual std::optional<uint64_t> dram_fast_access(
        [[maybe_unused]] uint32_t channel, [[maybe_unused]] uint64_t offset) {
        return std::nullopt;
    }
};

}  // namespace tt::umd
