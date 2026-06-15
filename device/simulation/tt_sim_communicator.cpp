// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/tt_sim_communicator.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/utils/error.hpp"

// NOLINTBEGIN.
#define DLSYM_FUNCTION(func_name)                                                                           \
    pfn_##func_name##_ = (decltype(pfn_##func_name##_))dlsym(libttsim_handle_, #func_name);                 \
    if (!pfn_##func_name##_) {                                                                              \
        UMD_THROW(error::RuntimeError, fmt::format("Failed to find symbol: {} {}", #func_name, dlerror())); \
    }

// NOLINTEND.

namespace tt::umd {

// Multichip shared-library state. When the loaded libttsim.so exports
// libttsim_create_device_by_id + libttsim_select_device_by_id, all
// TTSimCommunicators share a single dlopen of the .so so they also share its
// process-global state (eth_switch routing table, Device* registry).
void *TTSimCommunicator::s_shared_handle_ = nullptr;
int TTSimCommunicator::s_shared_refcount_ = 0;
bool TTSimCommunicator::s_sim_initialized_ = false;
std::mutex TTSimCommunicator::s_shared_init_mutex_;
std::mutex TTSimCommunicator::device_lock_;

TTSimCommunicator::TTSimCommunicator(
    const std::filesystem::path &simulator_directory, bool copy_sim_binary, uint32_t chip_id, uint32_t num_chips) :
    simulator_directory_(simulator_directory),
    copy_sim_binary_(copy_sim_binary),
    chip_id_(chip_id),
    num_chips_(num_chips) {}

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
    if (uses_shared_handle()) {
        // Shared dlopen -- decrement refcount. When last communicator
        // destructs, call libttsim_exit (which the deferred shutdown()
        // path didn't call), then dlclose. The simulator is process-global,
        // so libttsim_exit should run exactly once.
        std::lock_guard<std::mutex> lock(s_shared_init_mutex_);
        if (--s_shared_refcount_ == 0 && s_shared_handle_) {
            if (pfn_libttsim_exit_) {
                pfn_libttsim_exit_();
            }
            dlclose(s_shared_handle_);
            s_shared_handle_ = nullptr;
            s_sim_initialized_ = false;
        }
    } else if (libttsim_handle_) {
        // If initialize() threw after aliasing s_shared_handle_ (the probe keeps that handle) but
        // before committing to a shared-handle mode, libttsim_handle_ may equal s_shared_handle_. It is
        // owned by the shared-refcount path; dlclosing it here would leave other communicators with a
        // dangling s_shared_handle_.
        std::lock_guard<std::mutex> lock(s_shared_init_mutex_);
        if (libttsim_handle_ != s_shared_handle_) {
            dlclose(libttsim_handle_);
        }
    }
    close_simulator_binary();
}

void TTSimCommunicator::initialize() {
    std::lock_guard<std::mutex> lock(device_lock_);

    // Probe the .so for multichip ABI symbols. We do this before
    // committing to memfd-vs-direct dlopen because the shared-library path
    // skips memfd entirely (multiple chips share one dlopen).
    //
    // If the shared handle already exists, use it for the probe directly
    // to avoid dlopen/dlclose running global constructors/destructors.
    bool multichip_supported = false;
    {
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        if (s_shared_handle_) {
            // Already loaded by another communicator -- probe in-place.
            multichip_supported = dlsym(s_shared_handle_, "libttsim_create_device_by_id") != nullptr &&
                                  dlsym(s_shared_handle_, "libttsim_select_device_by_id") != nullptr;
        } else {
            // Fresh load: open, probe, and keep the handle if multichip is supported
            // to avoid a wasteful close+reopen cycle.
            void *probe = dlopen(simulator_directory_.c_str(), RTLD_LAZY);
            if (probe) {
                multichip_supported = dlsym(probe, "libttsim_create_device_by_id") != nullptr &&
                                      dlsym(probe, "libttsim_select_device_by_id") != nullptr;
                if (multichip_supported) {
                    // Keep this handle as the shared handle.
                    s_shared_handle_ = probe;
                } else {
                    dlclose(probe);
                }
            }
        }
    }

    if (multichip_supported) {
        log_info(tt::LogEmulationDriver, "TTSim multichip mode enabled (chip_id={}, shared dlopen)", chip_id_);
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        // s_shared_handle_ is guaranteed non-null here (set in probe above or by a prior communicator).
        libttsim_handle_ = s_shared_handle_;

        // Resolve all required multichip symbols via DLSYM_FUNCTION (throws on missing).
        DLSYM_FUNCTION(libttsim_init)
        DLSYM_FUNCTION(libttsim_exit)
        DLSYM_FUNCTION(libttsim_pci_config_rd32)
        DLSYM_FUNCTION(libttsim_pci_mem_rd_bytes)
        DLSYM_FUNCTION(libttsim_pci_mem_wr_bytes)
        DLSYM_FUNCTION(libttsim_tile_rd_bytes)
        DLSYM_FUNCTION(libttsim_tile_wr_bytes)
        DLSYM_FUNCTION(libttsim_clock)
        DLSYM_FUNCTION(libttsim_set_pci_dma_mem_callbacks)
        DLSYM_FUNCTION(libttsim_create_device_by_id)
        DLSYM_FUNCTION(libttsim_select_device_by_id)
        DLSYM_FUNCTION(libttsim_clock_all_devices)
        DLSYM_FUNCTION(libttsim_switch_reset)
        DLSYM_FUNCTION(libttsim_switch_register)
        DLSYM_FUNCTION(libttsim_switch_drain)
        DLSYM_FUNCTION(libttsim_configure_eth_link_virtual)
        DLSYM_FUNCTION(libttsim_switch_register_peer)

        // Optional extension symbols -- resolved with raw dlsym and allowed to be
        // nullptr.  These are newer additions to the libttsim ABI that not all .so
        // builds export yet; callers check for nullptr before use.
        pfn_libttsim_dram_rd_bytes_by_id_ = reinterpret_cast<decltype(pfn_libttsim_dram_rd_bytes_by_id_)>(
            dlsym(libttsim_handle_, "libttsim_dram_rd_bytes_by_id"));
        pfn_libttsim_dram_wr_bytes_by_id_ = reinterpret_cast<decltype(pfn_libttsim_dram_wr_bytes_by_id_)>(
            dlsym(libttsim_handle_, "libttsim_dram_wr_bytes_by_id"));
        pfn_libttsim_dram_core_rd_bytes_by_id_ = reinterpret_cast<decltype(pfn_libttsim_dram_core_rd_bytes_by_id_)>(
            dlsym(libttsim_handle_, "libttsim_dram_core_rd_bytes_by_id"));
        pfn_libttsim_dram_core_wr_bytes_by_id_ = reinterpret_cast<decltype(pfn_libttsim_dram_core_wr_bytes_by_id_)>(
            dlsym(libttsim_handle_, "libttsim_dram_core_wr_bytes_by_id"));
        pfn_libttsim_switch_register_fabric_node_id_ =
            reinterpret_cast<decltype(pfn_libttsim_switch_register_fabric_node_id_)>(
                dlsym(libttsim_handle_, "libttsim_switch_register_fabric_node_id"));
        pfn_libttsim_switch_register_fabric_endpoint_direction_ =
            reinterpret_cast<decltype(pfn_libttsim_switch_register_fabric_endpoint_direction_)>(
                dlsym(libttsim_handle_, "libttsim_switch_register_fabric_endpoint_direction"));

        // Only commit to multichip mode and bump refcount after ALL symbol resolution
        // has succeeded.  If a DLSYM_FUNCTION above throws, the destructor will
        // not attempt to decrement a refcount that was never incremented.
        multichip_mode_ = true;
        s_shared_refcount_++;
        return;
    }

    // No multichip ABI, but a multi-chip cluster: share a single dlopen so all chips
    // live in one libttsim process image (one shared per-chip state array), and address
    // each chip by its PCI device (BDF) + per-device BAR window. This is the BDF
    // host-enumeration path (docs/multichip/ARCHITECTURE.md) -- no select_device_by_id,
    // no virtual eth switch (the simulator routes inter-chip eth internally).
    if (num_chips_ > 1) {
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        if (!s_shared_handle_) {
            s_shared_handle_ = dlopen(simulator_directory_.c_str(), RTLD_LAZY);
            if (!s_shared_handle_) {
                UMD_THROW(error::RuntimeError, fmt::format("Failed to dlopen simulator library: {}", dlerror()));
            }
        }
        libttsim_handle_ = s_shared_handle_;
        DLSYM_FUNCTION(libttsim_init)
        DLSYM_FUNCTION(libttsim_exit)
        DLSYM_FUNCTION(libttsim_pci_config_rd32)
        DLSYM_FUNCTION(libttsim_pci_mem_rd_bytes)
        DLSYM_FUNCTION(libttsim_pci_mem_wr_bytes)
        DLSYM_FUNCTION(libttsim_tile_rd_bytes)
        DLSYM_FUNCTION(libttsim_tile_wr_bytes)
        DLSYM_FUNCTION(libttsim_clock)
        DLSYM_FUNCTION(libttsim_set_pci_dma_mem_callbacks)
        shared_bdf_mode_ = true;
        s_shared_refcount_++;
        log_info(
            tt::LogEmulationDriver, "TTSim BDF multichip mode (chip_id={}, shared dlopen, per-device BARs)", chip_id_);
        return;
    }

    // Legacy path: per-chip memfd + dlopen.
    if (copy_sim_binary_) {
        create_simulator_binary();
        copy_simulator_binary();
        secure_simulator_binary();
        load_simulator_library(fmt::format("/proc/self/fd/{}", copied_simulator_fd_));
    } else {
        load_simulator_library(simulator_directory_.string());
    }
}

void TTSimCommunicator::start_sim() {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (multichip_mode_) {
        // libttsim_init only on the first communicator. Subsequent
        // communicators register their chip into the shared registry.
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        if (!s_sim_initialized_) {
            pfn_libttsim_init_();
            s_sim_initialized_ = true;
        }
        // Register this chip in the shared chip_id registry.
        // Capture Device* handle for later eth-MAC registration.
        dev_handle_ = pfn_libttsim_create_device_by_id_(chip_id_, /*chip_x=*/int(chip_id_), /*chip_y=*/0);
        return;
    }
    if (shared_bdf_mode_) {
        // Shared dlopen: initialize the simulator exactly once. Chips are addressed by
        // BDF, so there is no per-chip registration call.
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        if (!s_sim_initialized_) {
            pfn_libttsim_init_();
            s_sim_initialized_ = true;
        }
        return;
    }
    pfn_libttsim_init_();
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
    if (uses_shared_handle()) {
        // Defer libttsim_exit until the last communicator destructs (handled
        // by refcount in the destructor's shared-handle close path). The
        // simulator is process-global; calling exit per chip would be wrong.
        return;
    }
    pfn_libttsim_exit_();
}

// Multichip mode: every I/O entry point first selects the right chip via
// libttsim_select_device_by_id(chip_id_) under the held device_lock_.
// The shared libttsim's internal recursive_mutex provides defense-in-depth.
void TTSimCommunicator::select_chip_if_needed() {
    if (multichip_mode_) {
        pfn_libttsim_select_device_by_id_(chip_id_);
    }
}

void TTSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogUMD, "Device writing {} bytes to l1_dest {} in core ({},{})", size, addr, x, y);
    select_chip_if_needed();
    pfn_libttsim_tile_wr_bytes_(x, y, addr, data, size);
}

void TTSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    select_chip_if_needed();
    pfn_libttsim_tile_rd_bytes_(x, y, addr, data, size);
}

bool TTSimCommunicator::dram_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    if (!multichip_mode_ || pfn_libttsim_dram_core_wr_bytes_by_id_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_dram_core_wr_bytes_by_id_(chip_id_, x, y, addr, data, size);
    return true;
}

bool TTSimCommunicator::dram_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    if (!multichip_mode_ || pfn_libttsim_dram_core_rd_bytes_by_id_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_dram_core_rd_bytes_by_id_(chip_id_, x, y, addr, data, size);
    return true;
}

void TTSimCommunicator::pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    select_chip_if_needed();
    pfn_libttsim_pci_mem_rd_bytes_(paddr, data, size);
}

void TTSimCommunicator::pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    select_chip_if_needed();
    pfn_libttsim_pci_mem_wr_bytes_(paddr, data, size);
}

uint32_t TTSimCommunicator::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    std::lock_guard<std::mutex> lock(device_lock_);
    // In BDF mode there is no select_device_by_id: this chip's PCI device is named by
    // its BDF (device field = chip_id), so each chip reads its own per-device BAR bases.
    // Callers pass bus_device_function 0 ("this device"); we fill in the device field.
    uint32_t bdf = bus_device_function;
    if (shared_bdf_mode_) {
        // BDF: function[2:0], device[7:3], bus[15:8]. The device field is only 5 bits, so chip_id >= 32
        // would silently overflow into the bus field and misroute. Fail loudly instead.
        UMD_ASSERT(chip_id_ < 32, error::RuntimeError, "BDF device field is 5 bits; chip_id must be < 32 in BDF mode.");
        bdf |= (chip_id_ << 3);
    }
    select_chip_if_needed();  // no-op outside the multichip-ABI mode
    return pfn_libttsim_pci_config_rd32_(bdf, offset);
}

void TTSimCommunicator::advance_clock(uint32_t n_clocks) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (multichip_mode_ && pfn_libttsim_clock_all_devices_) {
        // In shared-dlopen multichip mode, tick ALL chips together so
        // cross-chip eth handshakes converge (chip A waiting on a packet from chip B
        // would otherwise stall -- only the chip being read gets advanced).
        // libttsim_clock_all_devices walks the simulator chip_id registry internally
        // and drains the eth switch in sort order, so no extra switch_drain call.
        pfn_libttsim_clock_all_devices_(n_clocks);
        return;
    }
    select_chip_if_needed();
    pfn_libttsim_clock_(n_clocks);
}

TTSimCommunicator *TTSimCommunicator::callback_instance_ = nullptr;
std::array<TTSimCommunicator::DmaHostRange, TTSimCommunicator::kMaxDmaDevices> TTSimCommunicator::dma_ranges_{};
std::size_t TTSimCommunicator::dma_range_count_ = 0;
std::mutex TTSimCommunicator::dma_ranges_mutex_;

// Route a sysmem DMA to the owning MMIO chip by host address: the address the chip emits already went
// through its outbound iATU (UMD-programmed target = that chip's distinct host base), so we just find
// the registered host window [host_base, host_base+size) that contains it -- exactly as a host routes a
// DMA by which pinned region the physical address falls in. Rebase paddr to the within-window offset (so
// the per-chip callback sees an offset relative to its own host base, like the single-device case) and
// return the owning communicator. Falls back to callback_instance_ when no window matches (single
// device, or an address outside any registered region).
TTSimCommunicator *TTSimCommunicator::dma_route(uint64_t &paddr) {
    std::lock_guard<std::mutex> lock(dma_ranges_mutex_);
    for (std::size_t i = 0; i < dma_range_count_; i++) {
        const DmaHostRange &r = dma_ranges_[i];
        if (paddr >= r.host_base && paddr - r.host_base < r.host_size) {
            paddr -= r.host_base;
            return r.inst;
        }
    }
    return callback_instance_;
}

void TTSimCommunicator::pci_dma_mem_rd_bytes_wrapper(uint64_t paddr, void *p, uint32_t size) {
    TTSimCommunicator *inst = dma_route(paddr);
    if (inst && inst->pci_dma_mem_rd_bytes_callback_) {
        inst->pci_dma_mem_rd_bytes_callback_(paddr, p, size);
    }
}

void TTSimCommunicator::pci_dma_mem_wr_bytes_wrapper(uint64_t paddr, const void *p, uint32_t size) {
    TTSimCommunicator *inst = dma_route(paddr);
    if (inst && inst->pci_dma_mem_wr_bytes_callback_) {
        inst->pci_dma_mem_wr_bytes_callback_(paddr, p, size);
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
        // host_size==0 (legacy / single-device) registers nothing and relies on the callback_instance_
        // fallback.
        if (host_size != 0) {
            bool updated = false;
            for (std::size_t i = 0; i < dma_range_count_; i++) {
                if (dma_ranges_[i].inst == this) {  // re-registration: update in place
                    dma_ranges_[i] = {host_base, host_size, this};
                    updated = true;
                    break;
                }
            }
            if (!updated && dma_range_count_ < kMaxDmaDevices) {
                dma_ranges_[dma_range_count_++] = {host_base, host_size, this};
            }
        }
    }
    // The DMA callbacks are process-global, and libttsim forbids (re)registering them
    // once the simulator is running. With one shared simulator across chips, only the
    // first chip -- which runs libttsim_init in start_sim() -- registers them, before
    // init. Later chips just record their callback (last-writer-wins, the pre-existing
    // single-instance limitation) and skip the now-illegal re-registration.
    if (uses_shared_handle() && s_sim_initialized_) {
        return;
    }
    pfn_libttsim_set_pci_dma_mem_callbacks_(pci_dma_mem_rd_bytes_wrapper, pci_dma_mem_wr_bytes_wrapper);
}

void TTSimCommunicator::create_simulator_binary() {
    const std::string filename = simulator_directory_.stem().string();
    const std::string extension = simulator_directory_.extension().string();
    // Note: Using chip_id 0 for the communicator since it's not chip-specific.
    const std::string memfd_name = (filename + "_communicator" + extension);
    copied_simulator_fd_ = memfd_create(memfd_name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (copied_simulator_fd_ < 0) {
        UMD_THROW(error::RuntimeError, fmt::format("Failed to create memfd: {}", strerror(errno)));
    }
}

off_t TTSimCommunicator::resize_simulator_binary(int src_fd) {
    struct stat st;
    if (fstat(src_fd, &st) < 0) {
        close(src_fd);
        close_simulator_binary();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to get file size: {}", strerror(errno)));
    }
    off_t file_size = st.st_size;
    if (ftruncate(copied_simulator_fd_, file_size) < 0) {
        close(src_fd);
        close_simulator_binary();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to allocate space in memfd: {}", strerror(errno)));
    }
    return file_size;
}

void TTSimCommunicator::copy_simulator_binary() {
    int src_fd = open(simulator_directory_.c_str(), O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) {
        close_simulator_binary();
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "Failed to open simulator file for reading: {} - {}", simulator_directory_.string(), strerror(errno)));
    }
    off_t file_size = resize_simulator_binary(src_fd);
    off_t offset = 0;
    ssize_t bytes_copied = sendfile(copied_simulator_fd_, src_fd, &offset, file_size);
    close(src_fd);
    if (bytes_copied < 0) {
        close_simulator_binary();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to copy file with sendfile: {}", strerror(errno)));
    }
    if (bytes_copied != file_size) {
        close_simulator_binary();
        UMD_THROW(
            error::RuntimeError,
            fmt::format("Incomplete copy with sendfile: copied {} of {} bytes", bytes_copied, file_size));
    }
}

void TTSimCommunicator::secure_simulator_binary() {
    if (fcntl(copied_simulator_fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        close_simulator_binary();
        UMD_THROW(error::RuntimeError, fmt::format("Failed to seal memfd: {}", strerror(errno)));
    }
}

void TTSimCommunicator::load_simulator_library(const std::filesystem::path &path) {
    libttsim_handle_ = dlopen(path.c_str(), RTLD_LAZY);
    if (!libttsim_handle_) {
        close_simulator_binary();
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

// Multichip eth-MAC wiring methods. All no-ops in legacy single-chip mode.

void TTSimCommunicator::switch_reset() {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (multichip_mode_ && pfn_libttsim_switch_reset_) {
        pfn_libttsim_switch_reset_();
    }
}

void TTSimCommunicator::register_eth_endpoint(uint32_t eth_tile_id, uint64_t mac) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!multichip_mode_ || !dev_handle_) {
        return;
    }
    // Prefer configure_eth_link_virtual: sets link_mode=Virtual + writes
    // link-up sentinel + registers MAC. Falls back to switch_register if
    // configure_eth_link_virtual is not exported.
    if (pfn_libttsim_configure_eth_link_virtual_) {
        pfn_libttsim_configure_eth_link_virtual_(dev_handle_, eth_tile_id, mac);
    } else if (pfn_libttsim_switch_register_) {
        pfn_libttsim_switch_register_(dev_handle_, eth_tile_id, mac);
    }
}

void TTSimCommunicator::register_peer(uint32_t eth_tile_id, void *peer_dev, uint32_t peer_tile_id) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!multichip_mode_ || !dev_handle_ || !pfn_libttsim_switch_register_peer_ || !peer_dev) {
        return;
    }
    pfn_libttsim_switch_register_peer_(dev_handle_, eth_tile_id, peer_dev, peer_tile_id);
}

void TTSimCommunicator::register_fabric_node_id(uint32_t mesh_id, uint32_t chip_id) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!multichip_mode_ || !dev_handle_ || !pfn_libttsim_switch_register_fabric_node_id_) {
        return;
    }
    pfn_libttsim_switch_register_fabric_node_id_(dev_handle_, mesh_id, chip_id);
}

void TTSimCommunicator::register_fabric_endpoint_direction(uint32_t eth_tile_id, uint32_t direction) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!multichip_mode_ || !dev_handle_ || !pfn_libttsim_switch_register_fabric_endpoint_direction_) {
        return;
    }
    pfn_libttsim_switch_register_fabric_endpoint_direction_(dev_handle_, eth_tile_id, direction);
}

void TTSimCommunicator::switch_drain() {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (multichip_mode_ && pfn_libttsim_switch_drain_) {
        pfn_libttsim_switch_drain_();
    }
}

void TTSimCommunicator::close_simulator_binary() {
    if (copied_simulator_fd_ != -1) {
        close(copied_simulator_fd_);
        copied_simulator_fd_ = -1;
    }
}

}  // namespace tt::umd
