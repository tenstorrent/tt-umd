// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/backend/ttsim_backend.hpp"

#include <dlfcn.h>
#include <fmt/format.h>

#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "umd/device/simulation/backend/sim_backend_factory.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// Resolve a required symbol; throw with a clear message if the cooperative .so
// is missing a core entry point. Note: this resolves the *fixed* ABI surface,
// not optional features -- features are advertised via query_capabilities().
template <typename Fn>
static Fn resolve_required(void* handle, const char* name) {
    auto fn = reinterpret_cast<Fn>(dlsym(handle, name));
    if (fn == nullptr) {
        UMD_THROW(error::RuntimeError, fmt::format("libttsim missing required symbol '{}': {}", name, dlerror()));
    }
    return fn;
}

LibttsimModule::~LibttsimModule() {
    if (syms_.exit != nullptr) {
        syms_.exit();
    }
    if (handle_ != nullptr) {
        dlclose(handle_);
    }
}

std::shared_ptr<LibttsimModule> LibttsimModule::load(const std::filesystem::path& so_path) {
    auto module = std::shared_ptr<LibttsimModule>(new LibttsimModule());

    module->handle_ = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module->handle_ == nullptr) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen '{}': {}", so_path.string(), dlerror()));
    }
    void* h = module->handle_;
    Symbols& s = module->syms_;

    // Resolve the fixed ABI surface. These symbols are mandatory in every
    // cooperative libttsim; optional behavior is gated by capability bits below.
    s.variant = resolve_required<pfn_libttsim_variant>(h, "libttsim_variant");
    s.abi_version = resolve_required<pfn_libttsim_abi_version>(h, "libttsim_abi_version");
    s.query_capabilities = resolve_required<pfn_libttsim_query_capabilities>(h, "libttsim_query_capabilities");
    s.last_error = resolve_required<pfn_libttsim_last_error>(h, "libttsim_last_error");
    s.init = resolve_required<pfn_libttsim_init>(h, "libttsim_init");
    s.exit = resolve_required<pfn_libttsim_exit>(h, "libttsim_exit");
    s.get_arch = resolve_required<pfn_libttsim_get_arch>(h, "libttsim_get_arch");
    s.get_soc_descriptor = resolve_required<pfn_libttsim_get_soc_descriptor>(h, "libttsim_get_soc_descriptor");
    s.create_device = resolve_required<pfn_libttsim_create_device>(h, "libttsim_create_device");
    s.destroy_device = resolve_required<pfn_libttsim_destroy_device>(h, "libttsim_destroy_device");
    s.pci_config_rd32 = resolve_required<pfn_libttsim_pci_config_rd32>(h, "libttsim_pci_config_rd32");
    s.pci_mem_rd_bytes = resolve_required<pfn_libttsim_pci_mem_rd_bytes>(h, "libttsim_pci_mem_rd_bytes");
    s.pci_mem_wr_bytes = resolve_required<pfn_libttsim_pci_mem_wr_bytes>(h, "libttsim_pci_mem_wr_bytes");
    s.tile_rd_bytes = resolve_required<pfn_libttsim_tile_rd_bytes>(h, "libttsim_tile_rd_bytes");
    s.tile_wr_bytes = resolve_required<pfn_libttsim_tile_wr_bytes>(h, "libttsim_tile_wr_bytes");
    s.clock = resolve_required<pfn_libttsim_clock>(h, "libttsim_clock");
    s.set_pci_dma_mem_callbacks =
        resolve_required<pfn_libttsim_set_pci_dma_mem_callbacks>(h, "libttsim_set_pci_dma_mem_callbacks");

    // Identity + ABI compatibility gate. Refuse to run an incompatible sim.
    module->variant_ = s.variant();
    module->abi_version_ = s.abi_version();
    if (module->abi_version_ != LIBTTSIM_ABI_VERSION) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "libttsim ABI mismatch for '{}' (variant '{}'): UMD built for v{}, .so reports v{}",
                so_path.string(),
                module->variant_,
                LIBTTSIM_ABI_VERSION,
                module->abi_version_));
    }

    // Capabilities, self-declared -- no per-symbol probing.
    module->capabilities_ = s.query_capabilities();

    // Graceful init: status-returning, with retrievable detail.
    libttsim_status_t st = s.init();
    if (st != LIBTTSIM_OK) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("libttsim_init failed (status {}) for '{}': {}", (int)st, so_path.string(), s.last_error()));
    }

    log_info(
        tt::LogEmulationDriver,
        "Loaded libttsim '{}' variant='{}' abi=v{} caps=0x{:x}",
        so_path.string(),
        module->variant_,
        module->abi_version_,
        module->capabilities_);
    return module;
}

// ---------------------------------------------------------------------------
// TtsimBackend
// ---------------------------------------------------------------------------

std::unique_ptr<TtsimBackend> TtsimBackend::create(std::shared_ptr<LibttsimModule> module, uint32_t device_id) {
    const auto& s = module->syms();

    libttsim_device_t* dev = nullptr;
    libttsim_status_t st = s.create_device(device_id, &dev);
    if (st != LIBTTSIM_OK || dev == nullptr) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("libttsim_create_device({}) failed (status {}): {}", device_id, (int)st, s.last_error()));
    }

    // private ctor -> wrap in unique_ptr manually.
    std::unique_ptr<TtsimBackend> backend(new TtsimBackend(std::move(module), dev));

    // Self-description: arch + SOC descriptor come from the sim, not a sibling yaml.
    uint32_t arch_raw = 0;
    st = backend->module_->syms().get_arch(&arch_raw);
    if (st != LIBTTSIM_OK) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format("libttsim_get_arch failed (status {}): {}", (int)st, backend->module_->syms().last_error()));
    }
    backend->arch_ = static_cast<tt::ARCH>(arch_raw);

    // Two-shot buffer-too-small pattern for the SOC descriptor text.
    size_t needed = 0;
    st = backend->module_->syms().get_soc_descriptor(nullptr, 0, &needed);
    if (st != LIBTTSIM_OK && st != LIBTTSIM_ERR_BUFFER_TOO_SMALL) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "libttsim_get_soc_descriptor sizing failed (status {}): {}",
                (int)st,
                backend->module_->syms().last_error()));
    }
    std::vector<char> buf(needed + 1, '\0');
    st = backend->module_->syms().get_soc_descriptor(buf.data(), buf.size(), &needed);
    if (st != LIBTTSIM_OK) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "libttsim_get_soc_descriptor failed (status {}): {}", (int)st, backend->module_->syms().last_error()));
    }
    backend->soc_yaml_.assign(buf.data());

    return backend;
}

TtsimBackend::TtsimBackend(std::shared_ptr<LibttsimModule> module, libttsim_device_t* dev) :
    module_(std::move(module)), dev_(dev) {}

TtsimBackend::~TtsimBackend() {
    if (dev_ != nullptr) {
        module_->syms().destroy_device(dev_);
    }
}

void TtsimBackend::tile_read(uint32_t x, uint32_t y, uint64_t addr, void* dst, uint32_t size) {
    module_->syms().tile_rd_bytes(dev_, x, y, addr, dst, size);
}

void TtsimBackend::tile_write(uint32_t x, uint32_t y, uint64_t addr, const void* src, uint32_t size) {
    module_->syms().tile_wr_bytes(dev_, x, y, addr, src, size);
}

void TtsimBackend::pci_mem_read(uint64_t paddr, void* dst, uint32_t size) {
    module_->syms().pci_mem_rd_bytes(dev_, paddr, dst, size);
}

void TtsimBackend::pci_mem_write(uint64_t paddr, const void* src, uint32_t size) {
    module_->syms().pci_mem_wr_bytes(dev_, paddr, src, size);
}

uint32_t TtsimBackend::pci_config_read32(uint32_t bdf, uint32_t offset) {
    return module_->syms().pci_config_rd32(dev_, bdf, offset);
}

void TtsimBackend::advance_clock(uint32_t n_clocks) { module_->syms().clock(dev_, n_clocks); }

void TtsimBackend::dma_rd_trampoline(void* user, uint64_t addr, void* dst, uint32_t size) {
    auto* self = static_cast<TtsimBackend*>(user);
    if (self->dma_rd_) {
        self->dma_rd_(addr, dst, size);
    }
}

void TtsimBackend::dma_wr_trampoline(void* user, uint64_t addr, const void* src, uint32_t size) {
    auto* self = static_cast<TtsimBackend*>(user);
    if (self->dma_wr_) {
        self->dma_wr_(addr, src, size);
    }
}

void TtsimBackend::set_dma_callbacks(DmaReadFn rd, DmaWriteFn wr) {
    dma_rd_ = std::move(rd);
    dma_wr_ = std::move(wr);
    // `this` is the per-device user pointer: the sim hands it back to the
    // trampolines, so each device's callbacks stay independent.
    module_->syms().set_pci_dma_mem_callbacks(dev_, &TtsimBackend::dma_rd_trampoline, &TtsimBackend::dma_wr_trampoline, this);
}

// ---------------------------------------------------------------------------
// SimBackendFactory
// ---------------------------------------------------------------------------

std::unique_ptr<ISimBackend> SimBackendFactory::create(const std::filesystem::path& so_path, uint32_t device_id) {
    // Load once; handshake (variant + ABI gate + capabilities + init) happens here.
    std::shared_ptr<LibttsimModule> module = LibttsimModule::load(so_path);

    // Backend selection by self-declared variant. Today every cooperative sim
    // maps to TtsimBackend; the switch is the extension point for future
    // variants that need a different consumption strategy.
    const std::string& variant = module->variant();
    if (variant == "ttsim-private" || variant == "craq-sim") {
        return TtsimBackend::create(std::move(module), device_id);
    }

    UMD_THROW(error::RuntimeError, fmt::format("No UMD sim backend registered for variant '{}'", variant));
}

}  // namespace tt::umd
