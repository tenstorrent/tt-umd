// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/tt_sim_communicator.hpp"

#include <dlfcn.h>
#include <fmt/format.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "tt-kmd-lib/pci_ids.h"
#include "tt-umd/utils/error.hpp"

// NOLINTBEGIN.
#define DLSYM_FUNCTION(func_name)                                                                           \
    pfn_##func_name##_ = (decltype(pfn_##func_name##_))dlsym(libttsim_handle_, #func_name);                 \
    if (!pfn_##func_name##_) {                                                                              \
        UMD_THROW(error::RuntimeError, fmt::format("Failed to find symbol: {} {}", #func_name, dlerror())); \
    }

// NOLINTEND.

namespace tt::umd {

// Shared-library state for the shared BDF mode: when several chips are opened on one image, all
// TTSimCommunicators share a single dlopen of the .so so they also share the simulator it hosts.
std::weak_ptr<void> TTSimCommunicator::s_shared_lib_;
bool TTSimCommunicator::s_sim_initialized_ = false;
std::recursive_mutex TTSimCommunicator::s_shared_init_mutex_;
std::mutex TTSimCommunicator::device_lock_;

std::shared_ptr<void> TTSimCommunicator::adopt_shared_library(void *handle) {
    // libttsim_exit is resolved up front so the deleter never has to dlsym the handle it is about to
    // dlclose.  The shared mode requires the symbol, so it is non-null in practice.
    auto exit_fn = reinterpret_cast<void (*)()>(dlsym(handle, "libttsim_exit"));
    return std::shared_ptr<void>(handle, [exit_fn](void *h) {
        // Runs when the last owner drops its copy: the last communicator to destruct, or -- if symbol
        // resolution threw before any of them committed -- the reference initialize() held while
        // resolving them.  It also runs on this thread, with s_shared_init_mutex_ already held, if the
        // shared_ptr construction below throws while allocating its control block: the standard hands
        // the deleter the handle to release.  That is why the mutex is recursive.
        std::lock_guard<std::recursive_mutex> lock(s_shared_init_mutex_);
        // The simulator is process-global, so libttsim_exit runs exactly once -- and only if some
        // communicator got as far as start_sim() and actually called libttsim_init.
        if (s_sim_initialized_ && exit_fn) {
            exit_fn();
        }
        dlclose(h);
        s_sim_initialized_ = false;
    });
}

TTSimCommunicator::TTSimCommunicator(
    const std::filesystem::path &simulator_directory,
    uint32_t chip_id,
    uint32_t num_chips,
    std::optional<uint32_t> pci_bdf) :
    simulator_directory_(simulator_directory), chip_id_(chip_id), num_chips_(num_chips), pci_bdf_(pci_bdf) {
    // Every endpoint a simulator exposes is single-function, and the simulator rejects a BDF wider
    // than 16 bits fatally, so anything else did not come from enumerating it.
    UMD_ASSERT(
        !pci_bdf_.has_value() || (*pci_bdf_ <= 0xFFFF && (*pci_bdf_ & 0x7) == 0),
        error::RuntimeError,
        fmt::format(
            "Simulated PCI endpoint BDF 0x{:x} is not a function-0 bus/device/function.", pci_bdf_.value_or(0)));
}

TTSimCommunicator::~TTSimCommunicator() {
    // Unregister from the process-global DMA routing tables first. The shared simulator may still be
    // alive (other chips not yet destructed) and could emit a DMA; leaving a stale entry/callback
    // pointing at this freed communicator would route into freed memory (use-after-free).
    {
        std::lock_guard<std::mutex> ranges_lock(dma_ranges_mutex_);
        for (std::size_t i = 0; i < dma_range_count_; i++) {
            if (dma_ranges_[i].inst == this) {
                dma_ranges_[i] = dma_ranges_[dma_range_count_ - 1];  // compact: move last into the hole
                dma_ranges_[--dma_range_count_] = {};
                break;
            }
        }
        if (callback_instance_ == this) {
            callback_instance_ = nullptr;
        }
    }
    if (!shared_lib_ && libttsim_handle_) {
        // Legacy per-chip dlopen: this communicator owns the handle outright.  In the shared mode the
        // handle belongs to shared_lib_, and initialize() clears the raw view if it never committed.
        dlclose(libttsim_handle_);
    }
    // shared_lib_ is released once this body returns.  If this was the last communicator on the shared
    // dlopen, its deleter calls libttsim_exit (which the deferred shutdown() path skipped) and dlcloses.
}

void TTSimCommunicator::initialize() {
    std::lock_guard<std::mutex> lock(device_lock_);

    // Several chips opened: one libttsim .so image hosts them as PCIe chips, addressed by PCI device
    // (BDF) + per-device BAR window. All chips share a single dlopen (one shared per-chip state
    // array), and the simulator routes inter-chip eth internally.
    //
    // The library is loaded once per process, so every chip opened on it has to be one of its own
    // endpoints; start_sim() checks that against the running image.
    if (num_chips_ > 1) {
        std::lock_guard<std::recursive_mutex> init_lock(s_shared_init_mutex_);
        // This call's own reference until it commits below. If symbol resolution throws, unwinding
        // drops it, and when this call performed the dlopen that closes the library so a retry
        // starts clean.
        std::shared_ptr<void> shared_lib = s_shared_lib_.lock();
        if (!shared_lib) {
            void *handle = dlopen(simulator_directory_.c_str(), RTLD_LAZY);
            // Guard before wrapping: a shared_ptr built on nullptr would still run the teardown deleter.
            if (!handle) {
                UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen simulator library: {}", dlerror()));
            }
            shared_lib = adopt_shared_library(handle);
            s_shared_lib_ = shared_lib;
        }
        libttsim_handle_ = shared_lib.get();
        try {
            DLSYM_FUNCTION(libttsim_init)
            DLSYM_FUNCTION(libttsim_exit)
            DLSYM_FUNCTION(libttsim_pci_config_rd32)
            DLSYM_FUNCTION(libttsim_pci_mem_rd_bytes)
            DLSYM_FUNCTION(libttsim_pci_mem_wr_bytes)
            DLSYM_FUNCTION(libttsim_tile_rd_bytes)
            DLSYM_FUNCTION(libttsim_tile_wr_bytes)
            DLSYM_FUNCTION(libttsim_clock)
            DLSYM_FUNCTION(libttsim_set_pci_dma_mem_callbacks)
        } catch (...) {
            // Never committed, so no owning reference was taken.  Drop the raw view: the handle belongs
            // to the shared control block, and leaving it set would make the destructor dlclose a
            // library it does not own.
            libttsim_handle_ = nullptr;
            throw;
        }
        shared_bdf_mode_ = true;
        shared_lib_ = std::move(shared_lib);
        log_info(
            tt::LogEmulationDriver, "TTSim BDF multichip mode (chip_id={}, shared dlopen, per-device BARs)", chip_id_);
        return;
    }

    load_simulator_library(simulator_directory_.string());
}

void TTSimCommunicator::start_sim() {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (shared_bdf_mode_) {
        // Shared dlopen: initialize the simulator exactly once. Chips are addressed by
        // BDF, so there is no per-chip registration call.
        std::lock_guard<std::recursive_mutex> init_lock(s_shared_init_mutex_);
        if (!s_sim_initialized_) {
            pfn_libttsim_init_();
            s_sim_initialized_ = true;
        }
        resolve_pci_bdf();
        return;
    }
    pfn_libttsim_init_();
    resolve_pci_bdf();
}

void TTSimCommunicator::shutdown() {
    std::lock_guard<std::mutex> lock(device_lock_);
    // Guard against double-shutdown: close_device() calls mark_closed() then
    // shutdown(); the destructor also calls shutdown(). Without this check the
    // destructor would call pfn_libttsim_exit_() a second time.
    if (closed_) {
        return;
    }
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    if (shared_bdf_mode_) {
        // Defer libttsim_exit until the last communicator destructs (handled
        // by the shared_lib_ teardown deleter). The simulator is process-global;
        // calling exit per chip would be wrong.
        return;
    }
    pfn_libttsim_exit_();
}

void TTSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogUMD, "Device writing {} bytes to l1_dest {} in core ({},{})", size, addr, x, y);
    pfn_libttsim_tile_wr_bytes_(x, y, addr, data, size);
}

void TTSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_tile_rd_bytes_(x, y, addr, data, size);
}

void TTSimCommunicator::pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_pci_mem_rd_bytes_(paddr, data, size);
}

void TTSimCommunicator::pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_pci_mem_wr_bytes_(paddr, data, size);
}

void TTSimCommunicator::resolve_pci_bdf() {
    if (pci_bdf_.has_value()) {
        return;
    }
    // Nobody said where this chip's endpoint is, so find it the way enumerating the image would
    // have: chips are numbered in BDF order, and an image this communicator has to itself holds its
    // chip as the first endpoint. For a linear image that is bus 0, device chip_id (or device 0).
    const std::vector<uint32_t> bdfs = scan_pci_endpoints(pfn_libttsim_pci_config_rd32_);
    // Chips that share an image without being handed their BDFs were laid out by a cluster
    // descriptor, so a chip without an endpoint is the descriptor's doing. Checking the count as well
    // as this chip's id refuses on the first chip opened rather than after some are up.
    UMD_ASSERT(
        !shared_bdf_mode_ || (num_chips_ <= bdfs.size() && chip_id_ < bdfs.size()),
        error::RuntimeError,
        fmt::format(
            "Simulator {} exposes {} PCI endpoint(s), but the cluster descriptor opens {} chip(s) over PCIe on it, "
            "chip {} among them. Each needs its own endpoint, the chip id naming it in BDF order. Use a multichip "
            "simulator build, or drop the cluster descriptor and let UMD discover the topology.",
            simulator_directory_.string(),
            bdfs.size(),
            num_chips_,
            chip_id_));
    const uint32_t index = shared_bdf_mode_ ? chip_id_ : 0;
    UMD_ASSERT(
        index < bdfs.size(),
        error::RuntimeError,
        fmt::format(
            "Simulator {} exposes {} PCI endpoint(s), so chip {} has none of its own.",
            simulator_directory_.string(),
            bdfs.size(),
            chip_id_));
    // A shared image reached this way was sized from a cluster descriptor, whose chip ids say
    // nothing about where a sparse image placed each endpoint. Rather than bind a chip to whichever
    // endpoint happens to sort at its index, require the linear layout; a sparse image is opened by
    // enumerating it (create_local_simulation_tt_devices), which hands each chip its BDF.
    UMD_ASSERT(
        !shared_bdf_mode_ || bdfs[index] == (index << 3),
        error::RuntimeError,
        fmt::format(
            "Simulator {} places chip {}'s endpoint at BDF 0x{:x}, not bus 0 device {}: a sparse image "
            "cannot be opened from a cluster descriptor. Let UMD discover the cluster from the image "
            "instead.",
            simulator_directory_.string(),
            chip_id_,
            bdfs[index],
            index));
    pci_bdf_ = bdfs[index];
}

uint32_t TTSimCommunicator::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    std::lock_guard<std::mutex> lock(device_lock_);
    // This chip's PCI device is named by its BDF, so each chip reads its own per-device BAR bases.
    // Callers pass bus_device_function 0 ("this device"); we fill in this chip's endpoint.
    const uint32_t bdf = bus_device_function != 0 ? bus_device_function : pci_bdf_.value_or(0);
    return pfn_libttsim_pci_config_rd32_(bdf, offset);
}

const TTSimCommunicator::ImageProbe &TTSimCommunicator::probe_image(const std::filesystem::path &simulator_path) {
    std::lock_guard<std::recursive_mutex> init_lock(s_shared_init_mutex_);

    // An image's endpoints are fixed when it is built, so one bring-up answers every later question
    // about it -- including those asked once a simulator is running, when probing is no longer
    // possible. Keyed on the canonical path so every spelling of the same image finds it.
    static std::map<std::filesystem::path, ImageProbe> probes;
    const std::filesystem::path key = std::filesystem::weakly_canonical(simulator_path);
    if (auto cached = probes.find(key); cached != probes.end()) {
        return cached->second;
    }

    // This starts and stops an image of its own, so a simulator already running in this process
    // would be re-initialized -- fatal inside the simulator -- and then torn down underneath the
    // communicators still using it. Refuse instead, naming the ordering the caller has to keep.
    UMD_ASSERT(
        !s_sim_initialized_,
        error::RuntimeError,
        "Probing a simulator image must happen before any simulator is brought up in this process: a "
        "simulator is already initialized, and probing would re-initialize and then stop it.");

    void *handle = dlopen(simulator_path.c_str(), RTLD_LAZY);
    if (handle == nullptr) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen simulator library: {}", dlerror()));
    }
    // Closed however this returns: the throwing paths below would otherwise leak the handle.
    std::unique_ptr<void, int (*)(void *)> handle_guard(handle, &dlclose);

    auto config_read32 = reinterpret_cast<uint32_t (*)(uint32_t, uint32_t)>(dlsym(handle, "libttsim_pci_config_rd32"));
    auto sim_init = reinterpret_cast<void (*)()>(dlsym(handle, "libttsim_init"));
    auto sim_exit = reinterpret_cast<void (*)()>(dlsym(handle, "libttsim_exit"));
    if (config_read32 == nullptr || sim_init == nullptr || sim_exit == nullptr) {
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Simulator library {} does not export the symbols needed to probe it.", simulator_path.string()));
    }

    // Config space only answers while the image is running. Starting an image that is already
    // running is fatal inside the simulator, which is why the first probe of an image must run
    // before any simulator is brought up.
    sim_init();

    // Stopped even if the walk throws: config_read32 is documented to throw ConfigurationError,
    // and a left-running image makes the next probe or device open hit that fatal path.
    // Declared after handle_guard so the image stops before its library is unloaded.
    struct SimRunGuard {
        void (*exit_fn)();

        ~SimRunGuard() { exit_fn(); }
    } sim_run_guard{sim_exit};

    ImageProbe probe;
    probe.bdfs = scan_pci_endpoints(config_read32);
    if (!probe.bdfs.empty()) {
        probe.first_endpoint_id = config_read32(probe.bdfs.front(), 0);
    }
    return probes.emplace(key, std::move(probe)).first->second;
}

std::vector<uint32_t> TTSimCommunicator::enumerate_mmio_device_bdfs(const std::filesystem::path &simulator_path) {
    return probe_image(simulator_path).bdfs;
}

uint16_t TTSimCommunicator::read_pci_device_id(const std::filesystem::path &simulator_path) {
    const ImageProbe &probe = probe_image(simulator_path);
    UMD_ASSERT(
        !probe.bdfs.empty(),
        error::RuntimeError,
        fmt::format("Simulator {} exposes no host-visible PCI endpoints.", simulator_path.string()));
    const uint32_t vendor_and_device_id = probe.first_endpoint_id;
    const uint16_t vendor_id = static_cast<uint16_t>(vendor_and_device_id & 0xFFFF);
    UMD_ASSERT(
        vendor_id == TT_PCI_VENDOR_ID,
        error::RuntimeError,
        fmt::format(
            "Simulator {} reports PCI vendor id {:#x} rather than Tenstorrent's {:#x}.",
            simulator_path.string(),
            vendor_id,
            TT_PCI_VENDOR_ID));
    return static_cast<uint16_t>(vendor_and_device_id >> 16);
}

std::vector<uint32_t> TTSimCommunicator::scan_pci_endpoints(
    const std::function<uint32_t(uint32_t bus_device_function, uint32_t offset)> &config_read32) {
    // BDF: function[2:0], device[7:3], bus[15:8]. Simulated endpoints are single-function, so only
    // function 0 is probed.
    constexpr uint32_t ABSENT_ENDPOINT = 0xFFFFFFFF;
    constexpr uint32_t NUM_BUSES = 256;
    constexpr uint32_t DEVICES_PER_BUS = 32;
    std::vector<uint32_t> bdfs;
    auto scan_bus = [&](uint32_t bus) {
        for (uint32_t device = 0; device < DEVICES_PER_BUS; ++device) {
            const uint32_t bdf = (bus << 8) | (device << 3);
            if (config_read32(bdf, 0) != ABSENT_ENDPOINT) {
                bdfs.push_back(bdf);
            }
        }
    };

    scan_bus(0);
    // An endpoint at bus 0, device 0 means the linear layout, which never leaves bus 0. Probing
    // further would be harmless on a current image but exits the process on one built before sparse
    // layouts, and the two cannot be told apart -- see the header.
    if (!bdfs.empty() && bdfs.front() == 0) {
        return bdfs;
    }
    for (uint32_t bus = 1; bus < NUM_BUSES; ++bus) {
        scan_bus(bus);
    }
    return bdfs;
}

void TTSimCommunicator::advance_clock(uint32_t n_clocks) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_clock_(n_clocks);
}

TTSimCommunicator *TTSimCommunicator::callback_instance_ = nullptr;
std::array<TTSimCommunicator::DmaHostRange, TTSimCommunicator::MAX_DMA_DEVICES> TTSimCommunicator::dma_ranges_{};
std::size_t TTSimCommunicator::dma_range_count_ = 0;
std::mutex TTSimCommunicator::dma_ranges_mutex_;

// Route a sysmem DMA to the owning MMIO chip by host address: the address the chip emits already went
// through its outbound iATU (UMD-programmed target = that chip's distinct host base), so we just find
// the registered host window [host_base, host_base+size) that contains it -- exactly as a host routes a
// DMA by which pinned region the physical address falls in. Rebase physical_address to the within-window
// offset (so the per-chip callback sees an offset relative to its own host base, like the single-device
// case) and return the owning communicator.
//
// With 0 or 1 window registered (legacy / single-device, host_base 0), an unmatched address is harmless
// -- the mapping is an identity, so we fall back to callback_instance_. With more than one window
// (multichip), an address matching no window means the simulator emitted an out-of-region outbound
// address that never went through the iATU (e.g. a mapped-buffer arena IOVA, which sits above the
// channel grid that the iATU covers). We cannot rebase it to the right chip, and silently falling back
// would deliver it un-rebased to the wrong chip's sysmem. Hard-fail instead; extending iATU coverage to
// the arena (or routing it explicitly) is the real fix, tracked separately.
TTSimCommunicator *TTSimCommunicator::dma_route(uint64_t &physical_address) {
    std::lock_guard<std::mutex> lock(dma_ranges_mutex_);
    for (std::size_t i = 0; i < dma_range_count_; i++) {
        const DmaHostRange &range = dma_ranges_[i];
        if (physical_address >= range.host_base && (physical_address - range.host_base) < range.host_size) {
            physical_address -= range.host_base;
            return range.inst;
        }
    }
    UMD_ASSERT(
        dma_range_count_ <= 1,
        error::RuntimeError,
        fmt::format(
            "TTSim multichip DMA to host address 0x{:x} matches no registered chip window ({} windows registered). "
            "The simulator passed through an out-of-region outbound address (e.g. a mapped-buffer arena IOVA) that "
            "the outbound iATU does not cover, so it cannot be routed to the owning chip.",
            physical_address,
            dma_range_count_));
    return callback_instance_;
}

// Thread-safety note: dma_route() returns a communicator under dma_ranges_mutex_, then the callback is
// dereferenced here outside the lock. This is safe because DMA callbacks only fire synchronously from
// the owning chip's own thread while it drives the simulator (advance_clock / pci_mem_* under
// device_lock_) -- a chip cannot be issuing a DMA and running its own ~TTSimCommunicator (which
// unregisters the range under the same mutex) at the same time. No other thread holds a reference to
// the returned instance across the call.
void TTSimCommunicator::pci_dma_mem_rd_bytes_wrapper(uint64_t physical_address, void *p, uint32_t size) {
    TTSimCommunicator *dma_cb = dma_route(physical_address);
    if (dma_cb && dma_cb->pci_dma_mem_rd_bytes_callback_) {
        dma_cb->pci_dma_mem_rd_bytes_callback_(physical_address, p, size);
    }
}

void TTSimCommunicator::pci_dma_mem_wr_bytes_wrapper(uint64_t physical_address, const void *p, uint32_t size) {
    TTSimCommunicator *dma_cb = dma_route(physical_address);
    if (dma_cb && dma_cb->pci_dma_mem_wr_bytes_callback_) {
        dma_cb->pci_dma_mem_wr_bytes_callback_(physical_address, p, size);
    }
}

void TTSimCommunicator::set_pcie_dma_mem_callbacks(
    std::function<void(uint64_t, void *, uint32_t)> pfn_pci_dma_mem_rd_bytes,
    std::function<void(uint64_t, const void *, uint32_t)> pfn_pci_dma_mem_wr_bytes,
    uint64_t host_base,
    uint64_t host_size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pci_dma_mem_rd_bytes_callback_ = std::move(pfn_pci_dma_mem_rd_bytes);
    pci_dma_mem_wr_bytes_callback_ = std::move(pfn_pci_dma_mem_wr_bytes);
    // callback_instance_ and dma_ranges_ are both read by dma_route() under dma_ranges_mutex_, so they
    // must be written under that same mutex (not device_lock_) to avoid a data race with DMA callbacks.
    {
        std::lock_guard<std::mutex> ranges_lock(dma_ranges_mutex_);
        callback_instance_ = this;
        // Register this chip's host window for address-range DMA routing (dma_route()). The chip's
        // outbound iATU targets this window, so DMAs land here by address alone -- no per-chip tag.
        // Locate any window already registered for this instance so we can update or drop it in place.
        std::size_t existing = dma_range_count_;
        for (std::size_t i = 0; i < dma_range_count_; i++) {
            if (dma_ranges_[i].inst == this) {
                existing = i;
                break;
            }
        }
        if (host_size == 0) {
            // host_size==0 (legacy / single-device) registers no window and relies on the
            // callback_instance_ fallback. Drop any stale window this instance had registered, so a
            // re-register with host_size==0 doesn't leave a dangling range that misroutes DMAs.
            if (existing != dma_range_count_) {
                dma_ranges_[existing] = dma_ranges_[dma_range_count_ - 1];  // compact: move last into the hole
                dma_ranges_[--dma_range_count_] = {};
            }
        } else if (existing != dma_range_count_) {
            dma_ranges_[existing] = {host_base, host_size, this};  // re-registration: update in place
        } else {
            // New window. Overflowing the table would silently drop this chip's window and let dma_route
            // fall back to callback_instance_, delivering un-rebased host addresses to the wrong chip's
            // sysmem -- silent corruption. Make it a hard error instead.
            UMD_ASSERT(
                dma_range_count_ < MAX_DMA_DEVICES,
                error::RuntimeError,
                fmt::format(
                    "TTSim DMA host-range table full (MAX_DMA_DEVICES={}); cannot register another chip's "
                    "host window for DMA routing.",
                    MAX_DMA_DEVICES));
            dma_ranges_[dma_range_count_++] = {host_base, host_size, this};
        }
    }
    // The DMA callbacks are process-global, and libttsim forbids (re)registering them
    // once the simulator is running. With one shared simulator across chips, only the
    // first chip -- which runs libttsim_init in start_sim() -- registers them, before
    // init. Later chips just record their callback (last-writer-wins, the pre-existing
    // single-instance limitation) and skip the now-illegal re-registration.
    if (shared_bdf_mode_ && s_sim_initialized_) {
        return;
    }
    pfn_libttsim_set_pci_dma_mem_callbacks_(pci_dma_mem_rd_bytes_wrapper, pci_dma_mem_wr_bytes_wrapper);
}

void TTSimCommunicator::load_simulator_library(const std::filesystem::path &path) {
    libttsim_handle_ = dlopen(path.c_str(), RTLD_LAZY);
    if (!libttsim_handle_) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen simulator library: {}", dlerror()));
    }
    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_pci_config_rd32)
    DLSYM_FUNCTION(libttsim_pci_mem_rd_bytes)
    DLSYM_FUNCTION(libttsim_pci_mem_wr_bytes)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_clock)
    DLSYM_FUNCTION(libttsim_set_pci_dma_mem_callbacks)
}

}  // namespace tt::umd
