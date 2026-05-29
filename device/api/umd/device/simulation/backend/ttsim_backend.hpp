// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>

#include "umd/device/simulation/backend/libttsim_abi.h"
#include "umd/device/simulation/backend/sim_backend.hpp"

namespace tt::umd {

/**
 * LibttsimModule owns a dlopen'd cooperative libttsim.so and the resolved
 * symbol table. It is process/library-scoped (one per loaded .so), shared by
 * every device backend created from it. It performs the identity + ABI +
 * capability handshake exactly once, in load().
 *
 * Note what is NOT here: no per-feature dlsym probing (capabilities come from
 * libttsim_query_capabilities()), no soc_descriptor.yaml path logic, and no
 * static callback-instance pointer.
 */
class LibttsimModule {
public:
    ~LibttsimModule();

    // dlopen the .so, resolve the symbol table, verify variant + ABI version,
    // cache the capability mask, and call libttsim_init(). Throws on any failure,
    // surfacing libttsim_last_error() text.
    static std::shared_ptr<LibttsimModule> load(const std::filesystem::path& so_path);

    const std::string& variant() const { return variant_; }
    uint32_t abi_version() const { return abi_version_; }
    uint64_t capabilities() const { return capabilities_; }

    // Raw resolved symbol table (the concrete backend forwards through it).
    struct Symbols {
        pfn_libttsim_variant variant{};
        pfn_libttsim_abi_version abi_version{};
        pfn_libttsim_query_capabilities query_capabilities{};
        pfn_libttsim_last_error last_error{};
        pfn_libttsim_init init{};
        pfn_libttsim_exit exit{};
        pfn_libttsim_get_arch get_arch{};
        pfn_libttsim_get_soc_descriptor get_soc_descriptor{};
        pfn_libttsim_create_device create_device{};
        pfn_libttsim_destroy_device destroy_device{};
        pfn_libttsim_pci_config_rd32 pci_config_rd32{};
        pfn_libttsim_pci_mem_rd_bytes pci_mem_rd_bytes{};
        pfn_libttsim_pci_mem_wr_bytes pci_mem_wr_bytes{};
        pfn_libttsim_tile_rd_bytes tile_rd_bytes{};
        pfn_libttsim_tile_wr_bytes tile_wr_bytes{};
        pfn_libttsim_clock clock{};
        pfn_libttsim_set_pci_dma_mem_callbacks set_pci_dma_mem_callbacks{};
    };
    const Symbols& syms() const { return syms_; }

private:
    LibttsimModule() = default;

    void* handle_ = nullptr;
    Symbols syms_{};
    std::string variant_;
    uint32_t abi_version_ = 0;
    uint64_t capabilities_ = 0;
};

/**
 * TtsimBackend is the concrete ISimBackend for the cooperative libttsim ABI.
 *
 * It owns ONE libttsim_device_t* and passes it to every I/O call -- this is the
 * core of the design. There is no "current device" state anywhere; concurrent
 * devices are just independent TtsimBackend instances each holding their own
 * handle. DMA callbacks are registered with `this` as the user pointer, so the
 * static trampoline routes back to the right instance (no last-chip-wins bug).
 */
class TtsimBackend final : public ISimBackend {
public:
    // Creates a device on the (already loaded + initialized) module.
    static std::unique_ptr<TtsimBackend> create(std::shared_ptr<LibttsimModule> module, uint32_t device_id);
    ~TtsimBackend() override;

    std::string variant() const override { return module_->variant(); }
    uint32_t abi_version() const override { return module_->abi_version(); }
    uint64_t capabilities() const override { return module_->capabilities(); }
    bool has_capability(uint64_t cap_bit) const override { return (module_->capabilities() & cap_bit) != 0; }
    tt::ARCH arch() const override { return arch_; }
    std::string soc_descriptor_yaml() const override { return soc_yaml_; }

    void tile_read(uint32_t x, uint32_t y, uint64_t addr, void* dst, uint32_t size) override;
    void tile_write(uint32_t x, uint32_t y, uint64_t addr, const void* src, uint32_t size) override;
    void pci_mem_read(uint64_t paddr, void* dst, uint32_t size) override;
    void pci_mem_write(uint64_t paddr, const void* src, uint32_t size) override;
    uint32_t pci_config_read32(uint32_t bdf, uint32_t offset) override;
    void advance_clock(uint32_t n_clocks) override;
    void set_dma_callbacks(DmaReadFn rd, DmaWriteFn wr) override;

private:
    TtsimBackend(std::shared_ptr<LibttsimModule> module, libttsim_device_t* dev);

    // C trampolines: recover the TtsimBackend* from `user` and dispatch to the
    // std::function members. Per-instance routing, no globals.
    static void dma_rd_trampoline(void* user, uint64_t addr, void* dst, uint32_t size);
    static void dma_wr_trampoline(void* user, uint64_t addr, const void* src, uint32_t size);

    std::shared_ptr<LibttsimModule> module_;
    libttsim_device_t* dev_ = nullptr;  // this backend's own per-device handle
    tt::ARCH arch_ = tt::ARCH::Invalid;
    std::string soc_yaml_;
    DmaReadFn dma_rd_;
    DmaWriteFn dma_wr_;
};

}  // namespace tt::umd
