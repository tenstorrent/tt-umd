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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <ctime>
#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/types/arch.hpp"
#include "umd/device/utils/error.hpp"

// NOLINTBEGIN.
#define DLSYM_FUNCTION(func_name)                                                                           \
    pfn_##func_name##_ = (decltype(pfn_##func_name##_))dlsym(libttsim_handle_, #func_name);                 \
    if (!pfn_##func_name##_) {                                                                              \
        UMD_THROW(error::RuntimeError, fmt::format("Failed to find symbol: {} {}", #func_name, dlerror())); \
    }
#define DLSYM_OPTIONAL(func_name) pfn_##func_name##_ = (decltype(pfn_##func_name##_))dlsym(libttsim_handle_, #func_name);

// NOLINTEND.

namespace tt::umd {

// v3.5 multi-chip shared-library state. When the loaded libttsim.so exports
// libttsim_create_device_by_id + libttsim_select_device_by_id, all
// TTSimCommunicators share a single dlopen of the .so so they also share its
// process-global state (eth_switch routing table, Device* registry).
void *TTSimCommunicator::s_shared_handle_ = nullptr;
int TTSimCommunicator::s_shared_refcount_ = 0;
std::mutex TTSimCommunicator::s_shared_init_mutex_;
std::mutex TTSimCommunicator::device_lock_;

TTSimCommunicator::TTSimCommunicator(
    const std::filesystem::path &simulator_directory, bool copy_sim_binary, uint32_t chip_id) :
    simulator_directory_(simulator_directory), copy_sim_binary_(copy_sim_binary), chip_id_(chip_id) {}

TTSimCommunicator::~TTSimCommunicator() {
    if (v3_5_multichip_mode_) {
        // Shared dlopen — decrement refcount. When last communicator
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
        }
    } else if (libttsim_handle_) {
        dlclose(libttsim_handle_);
    }
    close_simulator_binary();
}

void TTSimCommunicator::initialize() {
    std::lock_guard<std::mutex> lock(device_lock_);

    // Probe the .so for v3.5 multichip ABI symbols. We do this before
    // committing to memfd-vs-direct dlopen because the shared-library path
    // skips memfd entirely (multiple chips share one dlopen).
    bool v3_5_supported = false;
    {
        // Temporarily dlopen just to check symbols; close after probe.
        void *probe = dlopen(simulator_directory_.c_str(), RTLD_LAZY | RTLD_NOLOAD);
        if (!probe) {
            // RTLD_NOLOAD fails if not yet loaded — that's fine, do a fresh probe.
            probe = dlopen(simulator_directory_.c_str(), RTLD_LAZY);
        }
        if (probe) {
            v3_5_supported = dlsym(probe, "libttsim_create_device_by_id") != nullptr &&
                             dlsym(probe, "libttsim_select_device_by_id") != nullptr;
            // Close only if we did the fresh open. RTLD_NOLOAD doesn't bump refcount
            // beyond existing, but to be safe always close our probe handle.
            dlclose(probe);
        }
    }

    if (v3_5_supported) {
        v3_5_multichip_mode_ = true;
        log_info(tt::LogEmulationDriver, "TTSim v3.5 multichip mode enabled (chip_id={}, shared dlopen)", chip_id_);
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        if (!s_shared_handle_) {
            s_shared_handle_ = dlopen(simulator_directory_.c_str(), RTLD_LAZY);
            if (!s_shared_handle_) {
                UMD_THROW(
                    error::RuntimeError, fmt::format("Failed to dlopen simulator library (shared): {}", dlerror()));
            }
        }
        s_shared_refcount_++;
        libttsim_handle_ = s_shared_handle_;
        DLSYM_FUNCTION(libttsim_init)
        DLSYM_FUNCTION(libttsim_exit)
        DLSYM_FUNCTION(libttsim_pci_config_rd32)
        DLSYM_FUNCTION(libttsim_pci_mem_rd_bytes)
        DLSYM_FUNCTION(libttsim_pci_mem_wr_bytes)
        DLSYM_FUNCTION(libttsim_tile_rd_bytes)
        DLSYM_FUNCTION(libttsim_tile_wr_bytes)
        pfn_libttsim_dram_rd_bytes_by_id_ =
            (decltype(pfn_libttsim_dram_rd_bytes_by_id_))dlsym(libttsim_handle_, "libttsim_dram_rd_bytes_by_id");
        pfn_libttsim_dram_wr_bytes_by_id_ =
            (decltype(pfn_libttsim_dram_wr_bytes_by_id_))dlsym(libttsim_handle_, "libttsim_dram_wr_bytes_by_id");
        pfn_libttsim_dram_core_rd_bytes_by_id_ = (decltype(pfn_libttsim_dram_core_rd_bytes_by_id_))dlsym(
            libttsim_handle_, "libttsim_dram_core_rd_bytes_by_id");
        pfn_libttsim_dram_core_wr_bytes_by_id_ = (decltype(pfn_libttsim_dram_core_wr_bytes_by_id_))dlsym(
            libttsim_handle_, "libttsim_dram_core_wr_bytes_by_id");
        pfn_libttsim_l1_rd_bytes_by_id_ = (decltype(pfn_libttsim_l1_rd_bytes_by_id_))dlsym(
            libttsim_handle_, "libttsim_l1_rd_bytes_by_id");
        pfn_libttsim_l1_wr_bytes_by_id_ = (decltype(pfn_libttsim_l1_wr_bytes_by_id_))dlsym(
            libttsim_handle_, "libttsim_l1_wr_bytes_by_id");
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
        pfn_libttsim_switch_register_fabric_node_id_ =
            (decltype(pfn_libttsim_switch_register_fabric_node_id_))dlsym(
                libttsim_handle_, "libttsim_switch_register_fabric_node_id");
        pfn_libttsim_switch_register_fabric_endpoint_direction_ =
            (decltype(pfn_libttsim_switch_register_fabric_endpoint_direction_))dlsym(
                libttsim_handle_, "libttsim_switch_register_fabric_endpoint_direction");
        DLSYM_OPTIONAL(libttsim_tensix_arm_launch_watcher)
        DLSYM_OPTIONAL(libttsim_erisc_arm_launch_watcher)
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
    if (v3_5_multichip_mode_) {
        // libttsim_init only on the first communicator. Subsequent
        // communicators register their chip into the shared registry.
        std::lock_guard<std::mutex> init_lock(s_shared_init_mutex_);
        static bool s_init_called = false;
        if (!s_init_called) {
            pfn_libttsim_init_();
            s_init_called = true;
        }
        // Register this chip in the shared chip_id registry.
        // v3.5 commit #6: capture Device* handle for later eth-MAC registration.
        dev_handle_ = pfn_libttsim_create_device_by_id_(chip_id_, /*chip_x=*/int(chip_id_), /*chip_y=*/0);
        return;
    }
    pfn_libttsim_init_();
}

void TTSimCommunicator::shutdown() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    if (v3_5_multichip_mode_) {
        // Defer libttsim_exit until the last communicator destructs (handled
        // by refcount in the destructor's shared-handle close path). The
        // simulator is process-global; calling exit per chip would be wrong.
        return;
    }
    pfn_libttsim_exit_();
}

// v3.5: in multi-chip mode, every I/O entry point first selects the right
// chip via libttsim_select_device_by_id(chip_id_) under the held device_lock_.
// The shared libttsim's internal recursive_mutex provides defense-in-depth.
#define SELECT_CHIP_IF_NEEDED()                                                \
    do {                                                                       \
        if (v3_5_multichip_mode_) pfn_libttsim_select_device_by_id_(chip_id_); \
    } while (0)

void TTSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogUMD, "Device writing {} bytes to l1_dest {} in core ({},{})", size, addr, x, y);
    SELECT_CHIP_IF_NEEDED();
    pfn_libttsim_tile_wr_bytes_(x, y, addr, data, size);
}

void TTSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    SELECT_CHIP_IF_NEEDED();
    pfn_libttsim_tile_rd_bytes_(x, y, addr, data, size);
}

bool TTSimCommunicator::dram_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    if (!v3_5_multichip_mode_ || pfn_libttsim_dram_core_wr_bytes_by_id_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_dram_core_wr_bytes_by_id_(chip_id_, x, y, addr, data, size);
    return true;
}

bool TTSimCommunicator::dram_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    if (!v3_5_multichip_mode_ || pfn_libttsim_dram_core_rd_bytes_by_id_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_dram_core_rd_bytes_by_id_(chip_id_, x, y, addr, data, size);
    return true;
}

bool TTSimCommunicator::l1_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    if (!v3_5_multichip_mode_ || pfn_libttsim_l1_wr_bytes_by_id_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_l1_wr_bytes_by_id_(chip_id_, x, y, addr, data, size);
    return true;
}

bool TTSimCommunicator::l1_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    if (!v3_5_multichip_mode_ || pfn_libttsim_l1_rd_bytes_by_id_ == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_l1_rd_bytes_by_id_(chip_id_, x, y, addr, data, size);
    return true;
}

void TTSimCommunicator::pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    SELECT_CHIP_IF_NEEDED();
    pfn_libttsim_pci_mem_rd_bytes_(paddr, data, size);
}

void TTSimCommunicator::pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    SELECT_CHIP_IF_NEEDED();
    pfn_libttsim_pci_mem_wr_bytes_(paddr, data, size);
}

uint32_t TTSimCommunicator::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    std::lock_guard<std::mutex> lock(device_lock_);
    // pci_config_read32 reads compile-time constants; no chip selection needed.
    // But we still select for consistency and future-proofing.
    SELECT_CHIP_IF_NEEDED();
    return pfn_libttsim_pci_config_rd32_(bus_device_function, offset);
}

void TTSimCommunicator::advance_clock(uint32_t n_clocks) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (v3_5_multichip_mode_ && pfn_libttsim_clock_all_devices_) {
        // v3.5-#8: in shared-dlopen multichip mode, tick ALL chips together so
        // cross-chip eth handshakes converge (chip A waiting on a packet from chip B
        // would otherwise stall — only the chip being read gets advanced).
        // libttsim_clock_all_devices walks the simulator chip_id registry internally
        // and drains the eth switch in sort order, so no extra switch_drain call.
        pfn_libttsim_clock_all_devices_(n_clocks);
        return;
    }
    SELECT_CHIP_IF_NEEDED();
    pfn_libttsim_clock_(n_clocks);
}

TTSimCommunicator *TTSimCommunicator::callback_instance_ = nullptr;

void TTSimCommunicator::pci_dma_mem_rd_bytes_wrapper(uint64_t paddr, void *p, uint32_t size) {
    if (callback_instance_ && callback_instance_->pci_dma_mem_rd_bytes_callback_) {
        callback_instance_->pci_dma_mem_rd_bytes_callback_(paddr, p, size);
    }
}

void TTSimCommunicator::pci_dma_mem_wr_bytes_wrapper(uint64_t paddr, const void *p, uint32_t size) {
    if (callback_instance_ && callback_instance_->pci_dma_mem_wr_bytes_callback_) {
        callback_instance_->pci_dma_mem_wr_bytes_callback_(paddr, p, size);
    }
}

void TTSimCommunicator::set_pcie_dma_mem_callbacks(
    std::function<void(uint64_t, void *, uint32_t)> pfn_pci_dma_mem_rd_bytes,
    std::function<void(uint64_t, const void *, uint32_t)> pfn_pci_dma_mem_wr_bytes) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pci_dma_mem_rd_bytes_callback_ = std::move(pfn_pci_dma_mem_rd_bytes);
    pci_dma_mem_wr_bytes_callback_ = std::move(pfn_pci_dma_mem_wr_bytes);
    callback_instance_ = this;
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
    DLSYM_OPTIONAL(libttsim_tensix_arm_launch_watcher)
    DLSYM_OPTIONAL(libttsim_erisc_arm_launch_watcher)
}

namespace {

std::optional<std::pair<uint32_t, uint32_t>> wh_remap_virtual_noc0(uint32_t x, uint32_t y) {
    switch (x) {
        case 18: x = 1; break;
        case 19: x = 2; break;
        case 20: x = 3; break;
        case 21: x = 4; break;
        case 22: x = 6; break;
        case 23: x = 7; break;
        case 24: x = 8; break;
        case 25: x = 9; break;
        default: break;
    }
    switch (y) {
        case 16: y = 0; break;
        case 17: y = 6; break;
        case 18: y = 1; break;
        case 19: y = 2; break;
        case 20: y = 3; break;
        case 21: y = 4; break;
        case 22: y = 5; break;
        case 23: y = 7; break;
        case 24: y = 8; break;
        case 25: y = 9; break;
        case 26: y = 10; break;
        case 27: y = 11; break;
        default: break;
    }
    if (x > 9 || y > 11) {
        return std::nullopt;
    }
    return std::make_optional(std::make_pair(x, y));
}

std::optional<uint32_t> wh_tile_id_from_physical(uint32_t x, uint32_t y, bool is_eth) {
    uint32_t tile_x = 0;
    if (x >= 1 && x <= 4) {
        tile_x = x - 1;
    } else if (x >= 6 && x <= 9) {
        tile_x = x - 2;
    } else {
        return std::nullopt;
    }
    if (is_eth) {
        if (y != 0 && y != 6) {
            return std::nullopt;
        }
        uint32_t tile_id = (tile_x < 4) ? (2 * tile_x + 1) : (6 - 2 * (tile_x - 4));
        if (y == 6) {
            tile_id += 8;
        }
        return tile_id;
    }
    if ((y >= 1 && y <= 5) || (y >= 7 && y <= 11)) {
        const uint32_t tile_y = (y <= 5) ? (y - 1) : (y - 2);
        return tile_x + tile_y * 8;
    }
    return std::nullopt;
}

std::optional<uint32_t> wh_tile_id_from_metal_coord(uint32_t x, uint32_t y, bool is_eth) {
    if (is_eth && y == 25 && x >= 20 && x <= 31) {
        return x - 20;
    }
    if (x >= 16 || y >= 16) {
        const auto remapped = wh_remap_virtual_noc0(x, y);
        if (!remapped.has_value()) {
            return std::nullopt;
        }
        return wh_tile_id_from_physical(remapped->first, remapped->second, is_eth);
    }
    return wh_tile_id_from_physical(x, y, is_eth);
}

std::optional<uint32_t> bh_tile_id_from_metal_coord(uint32_t x, uint32_t y, bool is_eth) {
    if (is_eth && y == 25 && x >= 20 && x <= 31) {
        return x - 20;
    }
    uint32_t tile_x = 0;
    if (x >= 1 && x <= 7) {
        tile_x = x - 1;
    } else if (x >= 10 && x <= 16) {
        tile_x = x - 3;
    } else {
        return std::nullopt;
    }
    if (is_eth) {
        if (y != 1) {
            return std::nullopt;
        }
        return (tile_x < 7) ? (2 * tile_x) : (13 - 2 * (tile_x - 7));
    }
    if (y >= 2 && y <= 11) {
        return tile_x + (y - 2) * 14;
    }
    return std::nullopt;
}

std::optional<uint32_t> sim_tile_id_from_noc(uint32_t x, uint32_t y, bool is_eth, tt::ARCH arch) {
    if (arch == tt::ARCH::WORMHOLE_B0) {
        return wh_tile_id_from_metal_coord(x, y, is_eth);
    }
    if (arch == tt::ARCH::BLACKHOLE) {
        return bh_tile_id_from_metal_coord(x, y, is_eth);
    }
    return std::nullopt;
}

}  // namespace

void TTSimCommunicator::arm_launch_watcher_for_noc_core(uint32_t noc_x, uint32_t noc_y, bool is_eth, tt::ARCH arch) {
    const auto tile_id = sim_tile_id_from_noc(noc_x, noc_y, is_eth, arch);
    // #region agent log
    {
        std::FILE* f = std::fopen("/data/rsong/tt-metal-fork/.cursor/debug-ae7d0a.log", "a");
        if (f) {
            std::fprintf(
                f,
                "{\"sessionId\":\"ae7d0a\",\"hypothesisId\":\"H_ARM_WATCHER\","
                "\"location\":\"tt_sim_communicator.cpp:arm_launch_watcher_for_noc_core\","
                "\"message\":\"%s\",\"data\":{\"chip\":%u,\"noc_x\":%u,\"noc_y\":%u,\"is_eth\":%d,"
                "\"tile_id\":%u,\"has_dev\":%d,\"arch\":%u,\"tensix_fn\":%d,\"erisc_fn\":%d,\"pid\":%d},"
                "\"timestamp\":%ld}\n",
                tile_id.has_value() && dev_handle_ ? "ARMED" : "SKIP",
                chip_id_,
                noc_x,
                noc_y,
                is_eth ? 1 : 0,
                tile_id.value_or(9999),
                dev_handle_ != nullptr,
                static_cast<unsigned>(arch),
                pfn_libttsim_tensix_arm_launch_watcher_ != nullptr,
                pfn_libttsim_erisc_arm_launch_watcher_ != nullptr,
                (int)getpid(),
                (long)std::time(nullptr));
            std::fclose(f);
        }
    }
    // #endregion
    if (!tile_id.has_value() || !dev_handle_) {
        return;
    }
    std::lock_guard<std::mutex> lock(device_lock_);
    if (v3_5_multichip_mode_) {
        pfn_libttsim_select_device_by_id_(chip_id_);
    }
    if (is_eth) {
        if (pfn_libttsim_erisc_arm_launch_watcher_) {
            pfn_libttsim_erisc_arm_launch_watcher_(dev_handle_, *tile_id);
        }
    } else if (pfn_libttsim_tensix_arm_launch_watcher_) {
        pfn_libttsim_tensix_arm_launch_watcher_(dev_handle_, *tile_id);
    }
}

// v3.5 commit #6 — eth-MAC wiring methods. All no-ops in legacy single-chip mode.

void TTSimCommunicator::switch_reset() {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (v3_5_multichip_mode_ && pfn_libttsim_switch_reset_) {
        pfn_libttsim_switch_reset_();
    }
}

void TTSimCommunicator::register_eth_endpoint(uint32_t eth_tile_id, uint64_t mac) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!v3_5_multichip_mode_ || !dev_handle_) {
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
    register_peer_on_devices(dev_handle_, eth_tile_id, peer_dev, peer_tile_id);
}

void *TTSimCommunicator::get_or_create_device_handle(uint32_t chip_id, int chip_x, int chip_y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!v3_5_multichip_mode_ || !pfn_libttsim_create_device_by_id_) {
        return nullptr;
    }
    return pfn_libttsim_create_device_by_id_(chip_id, chip_x, chip_y);
}

void TTSimCommunicator::register_eth_endpoint_on_device(void *dev, uint32_t eth_tile_id, uint64_t mac) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!v3_5_multichip_mode_ || !dev) {
        return;
    }
    if (pfn_libttsim_configure_eth_link_virtual_) {
        pfn_libttsim_configure_eth_link_virtual_(dev, eth_tile_id, mac);
    } else if (pfn_libttsim_switch_register_) {
        pfn_libttsim_switch_register_(dev, eth_tile_id, mac);
    }
}

void TTSimCommunicator::register_peer_on_devices(
    void *dev, uint32_t eth_tile_id, void *peer_dev, uint32_t peer_tile_id) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (!v3_5_multichip_mode_ || !dev || !peer_dev || !pfn_libttsim_switch_register_peer_) {
        return;
    }
    pfn_libttsim_switch_register_peer_(dev, eth_tile_id, peer_dev, peer_tile_id);
}

void TTSimCommunicator::register_fabric_node_id(uint32_t mesh_id, uint32_t chip_id) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (std::getenv("TTSIM_FABRIC_TERMINAL_TRACE")) {
        fprintf(
            stderr,
            "[ttsim-fabric-terminal] umd-register-node chip=%u dev=%p fabric=(%u,%u) mode=%u pfn=%u\n",
            chip_id_,
            dev_handle_,
            mesh_id,
            chip_id,
            v3_5_multichip_mode_ ? 1u : 0u,
            pfn_libttsim_switch_register_fabric_node_id_ ? 1u : 0u);
    }
    if (!v3_5_multichip_mode_ || !dev_handle_ || !pfn_libttsim_switch_register_fabric_node_id_) return;
    pfn_libttsim_switch_register_fabric_node_id_(dev_handle_, mesh_id, chip_id);
}

void TTSimCommunicator::register_fabric_endpoint_direction(uint32_t eth_tile_id, uint32_t direction) {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (std::getenv("TTSIM_FABRIC_TERMINAL_TRACE")) {
        fprintf(
            stderr,
            "[ttsim-fabric-terminal] umd-register-direction chip_id=%u tile=E%u dir=%u v3_5=%u dev=%p sym=%p\n",
            chip_id_,
            eth_tile_id,
            direction,
            v3_5_multichip_mode_ ? 1u : 0u,
            dev_handle_,
            reinterpret_cast<void*>(pfn_libttsim_switch_register_fabric_endpoint_direction_));
    }
    if (!v3_5_multichip_mode_ || !dev_handle_ || !pfn_libttsim_switch_register_fabric_endpoint_direction_) return;
    pfn_libttsim_switch_register_fabric_endpoint_direction_(dev_handle_, eth_tile_id, direction);
}

void TTSimCommunicator::switch_drain() {
    std::lock_guard<std::mutex> lock(device_lock_);
    if (v3_5_multichip_mode_ && pfn_libttsim_switch_drain_) {
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
