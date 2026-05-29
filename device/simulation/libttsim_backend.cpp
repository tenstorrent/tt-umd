// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/libttsim_backend.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/utils/error.hpp"

namespace tt::umd {

// =====================================================================================
// The C ABI of TODAY's libttsim.so. These are the ONLY place raw symbol names appear for
// this backend; everything above the backend is symbol-agnostic.
// =====================================================================================
namespace symbol {
constexpr const char *INIT = "libttsim_init";
constexpr const char *EXIT = "libttsim_exit";
constexpr const char *SET_DMA_CB = "libttsim_set_pci_dma_mem_callbacks";
constexpr const char *PCI_CFG_RD32 = "libttsim_pci_config_rd32";
constexpr const char *PCI_CFG_WR32 = "libttsim_pci_config_wr32";
constexpr const char *PCI_MEM_RD = "libttsim_pci_mem_rd_bytes";
constexpr const char *PCI_MEM_WR = "libttsim_pci_mem_wr_bytes";
constexpr const char *TILE_RD = "libttsim_tile_rd_bytes";
constexpr const char *TILE_WR = "libttsim_tile_wr_bytes";
constexpr const char *CLOCK = "libttsim_clock";

// Multichip symbols that this backend WOULD need to honor SimCapability::Multichip. They are
// NOT exported by today's libttsim.so, so the capability resolves to false. Listing them here
// documents the intended contract and makes the future divergence point explicit.
constexpr const char *CREATE_DEVICE = "libttsim_create_device_by_id";
constexpr const char *SELECT_DEVICE = "libttsim_select_device_by_id";
constexpr const char *CLOCK_ALL = "libttsim_clock_all_devices";

// Illustrative optional fast-DRAM symbol, also absent today.
constexpr const char *DRAM_FAST = "libttsim_dram_direct_window";
}  // namespace symbol

// =====================================================================================
// Capability table: capability -> exact set of functions required to claim it.
// A capability is present iff EVERY function in its set resolves (all-or-nothing).
// =====================================================================================
static const std::initializer_list<CapabilityReq> kCapabilityTable = {
    {SimCapability::CoreIo, {symbol::INIT, symbol::EXIT, symbol::CLOCK, symbol::TILE_RD, symbol::TILE_WR}},
    {SimCapability::PcieAccess, {symbol::PCI_MEM_RD, symbol::PCI_MEM_WR, symbol::PCI_CFG_RD32, symbol::PCI_CFG_WR32}},
    {SimCapability::DmaCallbacks, {symbol::SET_DMA_CB}},
    // Divergent feature: absent on today's .so because these three symbols don't exist.
    {SimCapability::Multichip, {symbol::CREATE_DEVICE, symbol::SELECT_DEVICE, symbol::CLOCK_ALL}},
    {SimCapability::DramFastAccess, {symbol::DRAM_FAST}},
};

// Superset of every symbol the table references; resolved once at load time.
static std::vector<const char *> all_known_symbols() {
    std::vector<const char *> out;
    for (const auto &req : kCapabilityTable) {
        for (const char *fn : req.required_fns) {
            out.push_back(fn);
        }
    }
    return out;
}

const std::vector<const char *> &LibTTSimBackend::minimum_required_symbols() {
    // To be a candidate at selection time, this backend needs the baseline single-chip +
    // PCIe + clock ABI of today's libttsim.so. Multichip / dram-fast are explicitly NOT
    // required (they are optional capabilities layered on top).
    static const std::vector<const char *> kMin = {
        symbol::INIT,
        symbol::EXIT,
        symbol::SET_DMA_CB,
        symbol::PCI_CFG_RD32,
        symbol::PCI_MEM_RD,
        symbol::PCI_MEM_WR,
        symbol::TILE_RD,
        symbol::TILE_WR,
        symbol::CLOCK,
    };
    return kMin;
}

LibTTSimBackend::LibTTSimBackend(const std::filesystem::path &simulator_path, bool copy_sim_binary) :
    simulator_path_(simulator_path), copy_sim_binary_(copy_sim_binary) {
    load_library();
    resolve_symbols_and_capabilities();
}

LibTTSimBackend::~LibTTSimBackend() {
    if (handle_) {
        dlclose(handle_);
    }
    close_memfd();
}

void LibTTSimBackend::load_library() {
    if (copy_sim_binary_) {
        create_memfd_copy();
        copy_into_memfd();
        seal_memfd();
        std::string proc_path = fmt::format("/proc/self/fd/{}", memfd_);
        handle_ = dlopen(proc_path.c_str(), RTLD_LAZY);
    } else {
        handle_ = dlopen(simulator_path_.c_str(), RTLD_LAZY);
    }
    if (!handle_) {
        close_memfd();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen simulator library: {}", dlerror()));
    }
}

void LibTTSimBackend::resolve_symbols_and_capabilities() {
    // 1) Best-effort resolve every symbol the capability table references. Unresolved symbols
    //    are simply absent from the map (no throw) — capability gating decides what matters.
    for (const char *name : all_known_symbols()) {
        if (symbols_.count(name)) {
            continue;
        }
        if (void *p = dlsym(handle_, name)) {
            symbols_[name] = p;
        }
    }

    // 2) For each capability, it is present iff EVERY required function resolved. All-or-nothing:
    //    a half-resolved set => unsupported, never half-wired.
    for (const auto &req : kCapabilityTable) {
        bool all_present = true;
        for (const char *name : req.required_fns) {
            if (symbols_.find(name) == symbols_.end()) {
                all_present = false;
                break;
            }
        }
        capabilities_[req.cap] = all_present;
        log_debug(
            tt::LogEmulationDriver,
            "LibTTSimBackend capability {} -> {}",
            static_cast<int>(req.cap),
            all_present ? "supported" : "unsupported");
    }

    // The baseline capabilities must resolve, otherwise this is not a usable libttsim.so.
    UMD_ASSERT(
        supports(SimCapability::CoreIo) && supports(SimCapability::PcieAccess) &&
            supports(SimCapability::DmaCallbacks),
        error::RuntimeError,
        "libttsim.so is missing baseline symbols required for core I/O / PCIe / DMA.");
}

bool LibTTSimBackend::supports(SimCapability cap) const {
    auto it = capabilities_.find(cap);
    return it != capabilities_.end() && it->second;
}

template <typename Fn>
Fn LibTTSimBackend::fn(const char *name) const {
    auto it = symbols_.find(name);
    UMD_ASSERT(it != symbols_.end(), error::RuntimeError, fmt::format("Symbol not resolved: {}", name));
    return reinterpret_cast<Fn>(it->second);
}

// ---- ISimBackend ops, expressed against resolved pointers ----

void LibTTSimBackend::init() { fn<void (*)()>(symbol::INIT)(); }

void LibTTSimBackend::shutdown() {
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    fn<void (*)()>(symbol::EXIT)();
}

void LibTTSimBackend::tile_read(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    fn<void (*)(uint32_t, uint32_t, uint64_t, void *, uint32_t)>(symbol::TILE_RD)(x, y, addr, data, size);
}

void LibTTSimBackend::tile_write(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    fn<void (*)(uint32_t, uint32_t, uint64_t, const void *, uint32_t)>(symbol::TILE_WR)(x, y, addr, data, size);
}

void LibTTSimBackend::pci_mem_read(uint64_t paddr, void *data, uint32_t size) {
    fn<void (*)(uint64_t, void *, uint32_t)>(symbol::PCI_MEM_RD)(paddr, data, size);
}

void LibTTSimBackend::pci_mem_write(uint64_t paddr, const void *data, uint32_t size) {
    fn<void (*)(uint64_t, const void *, uint32_t)>(symbol::PCI_MEM_WR)(paddr, data, size);
}

uint32_t LibTTSimBackend::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    return fn<uint32_t (*)(uint32_t, uint32_t)>(symbol::PCI_CFG_RD32)(bus_device_function, offset);
}

void LibTTSimBackend::advance_clock(uint32_t n_clocks) { fn<void (*)(uint32_t)>(symbol::CLOCK)(n_clocks); }

LibTTSimBackend *LibTTSimBackend::callback_instance_ = nullptr;

void LibTTSimBackend::dma_rd_trampoline(uint64_t paddr, void *p, uint32_t size) {
    if (callback_instance_ && callback_instance_->dma_rd_) {
        callback_instance_->dma_rd_(paddr, p, size);
    }
}

void LibTTSimBackend::dma_wr_trampoline(uint64_t paddr, const void *p, uint32_t size) {
    if (callback_instance_ && callback_instance_->dma_wr_) {
        callback_instance_->dma_wr_(paddr, p, size);
    }
}

void LibTTSimBackend::set_dma_callbacks(
    std::function<void(uint64_t, void *, uint32_t)> rd, std::function<void(uint64_t, const void *, uint32_t)> wr) {
    dma_rd_ = std::move(rd);
    dma_wr_ = std::move(wr);
    callback_instance_ = this;
    using SetCbFn = void (*)(
        void (*)(uint64_t, void *, uint32_t), void (*)(uint64_t, const void *, uint32_t));
    fn<SetCbFn>(symbol::SET_DMA_CB)(dma_rd_trampoline, dma_wr_trampoline);
}

// ---- memfd binary-copy helpers (moved verbatim in spirit from TTSimCommunicator) ----

void LibTTSimBackend::create_memfd_copy() {
    const std::string memfd_name = simulator_path_.stem().string() + "_backend" + simulator_path_.extension().string();
    memfd_ = memfd_create(memfd_name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (memfd_ < 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to create memfd: {}", strerror(errno)));
    }
}

off_t LibTTSimBackend::resize_memfd(int src_fd) {
    struct stat st {};
    if (fstat(src_fd, &st) < 0) {
        close(src_fd);
        close_memfd();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to get file size: {}", strerror(errno)));
    }
    if (ftruncate(memfd_, st.st_size) < 0) {
        close(src_fd);
        close_memfd();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to allocate space in memfd: {}", strerror(errno)));
    }
    return st.st_size;
}

void LibTTSimBackend::copy_into_memfd() {
    int src_fd = open(simulator_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) {
        close_memfd();
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Failed to open simulator file: {} - {}", simulator_path_.string(), strerror(errno)));
    }
    off_t file_size = resize_memfd(src_fd);
    off_t offset = 0;
    ssize_t bytes_copied = sendfile(memfd_, src_fd, &offset, file_size);
    close(src_fd);
    if (bytes_copied != file_size) {
        close_memfd();
        UMD_THROW(error::RuntimeError, fmt::format("Incomplete sendfile copy ({} of {})", bytes_copied, file_size));
    }
}

void LibTTSimBackend::seal_memfd() {
    if (fcntl(memfd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        close_memfd();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to seal memfd: {}", strerror(errno)));
    }
}

void LibTTSimBackend::close_memfd() {
    if (memfd_ != -1) {
        close(memfd_);
        memfd_ = -1;
    }
}

}  // namespace tt::umd
