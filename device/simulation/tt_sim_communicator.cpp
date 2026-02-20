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
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

// NOLINTBEGIN.
#define DLSYM_FUNCTION(func_name)                                                           \
    pfn_##func_name##_ = (decltype(pfn_##func_name##_))dlsym(libttsim_handle_, #func_name); \
    if (!pfn_##func_name##_) {                                                              \
        TT_THROW("Failed to find symbol: ", #func_name, dlerror());                         \
    }

// NOLINTEND.

namespace tt::umd {

TTSimCommunicator::TTSimCommunicator(const std::filesystem::path &simulator_directory, bool copy_sim_binary) :
    simulator_directory_(simulator_directory), copy_sim_binary_(copy_sim_binary) {}

TTSimCommunicator::~TTSimCommunicator() {
    if (libttsim_handle_) {
        dlclose(libttsim_handle_);
    }
    close_simulator_binary();
}

void TTSimCommunicator::initialize() {
    std::lock_guard<std::mutex> lock(device_lock_);

    if (copy_sim_binary_) {
        create_simulator_binary();
        copy_simulator_binary();
        secure_simulator_binary();
        load_simulator_library(fmt::format("/proc/self/fd/{}", copied_simulator_fd_));
    } else {
        load_simulator_library(simulator_directory_.string());
    }

    pfn_libttsim_init_();
}

void TTSimCommunicator::shutdown() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
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

uint32_t TTSimCommunicator::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    std::lock_guard<std::mutex> lock(device_lock_);
    return pfn_libttsim_pci_config_rd32_(bus_device_function, offset);
}

void TTSimCommunicator::advance_clock(uint32_t n_clocks) {
    std::lock_guard<std::mutex> lock(device_lock_);
    pfn_libttsim_clock_(n_clocks);
}

void TTSimCommunicator::create_simulator_binary() {
    const std::string filename = simulator_directory_.stem().string();
    const std::string extension = simulator_directory_.extension().string();
    // Note: Using chip_id 0 for the communicator since it's not chip-specific.
    const std::string memfd_name = (filename + "_communicator" + extension);
    copied_simulator_fd_ = memfd_create(memfd_name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (copied_simulator_fd_ < 0) {
        TT_THROW("Failed to create memfd: {}", strerror(errno));
    }
}

off_t TTSimCommunicator::resize_simulator_binary(int src_fd) {
    struct stat st;
    if (fstat(src_fd, &st) < 0) {
        close(src_fd);
        close_simulator_binary();
        TT_THROW("Failed to get file size: {}", strerror(errno));
    }
    off_t file_size = st.st_size;
    if (ftruncate(copied_simulator_fd_, file_size) < 0) {
        close(src_fd);
        close_simulator_binary();
        TT_THROW("Failed to allocate space in memfd: {}", strerror(errno));
    }
    return file_size;
}

void TTSimCommunicator::copy_simulator_binary() {
    int src_fd = open(simulator_directory_.c_str(), O_RDONLY | O_CLOEXEC);
    if (src_fd < 0) {
        close_simulator_binary();
        TT_THROW("Failed to open simulator file for reading: {} - {}", simulator_directory_.string(), strerror(errno));
    }
    off_t file_size = resize_simulator_binary(src_fd);
    off_t offset = 0;
    ssize_t bytes_copied = sendfile(copied_simulator_fd_, src_fd, &offset, file_size);
    close(src_fd);
    if (bytes_copied < 0) {
        close_simulator_binary();
        TT_THROW("Failed to copy file with sendfile: {}", strerror(errno));
    }
    if (bytes_copied != file_size) {
        close_simulator_binary();
        TT_THROW("Incomplete copy with sendfile: copied {} of {} bytes", bytes_copied, file_size);
    }
}

void TTSimCommunicator::secure_simulator_binary() {
    if (fcntl(copied_simulator_fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        close_simulator_binary();
        TT_THROW("Failed to seal memfd: {}", strerror(errno));
    }
}

void TTSimCommunicator::load_simulator_library(const std::filesystem::path &path) {
    libttsim_handle_ = dlopen(path.c_str(), RTLD_LAZY);
    if (!libttsim_handle_) {
        close_simulator_binary();
        TT_THROW("Failed to dlopen simulator library: {}", dlerror());
    }
    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_pci_config_rd32)
    DLSYM_FUNCTION(libttsim_pci_mem_rd_bytes)
    DLSYM_FUNCTION(libttsim_pci_mem_wr_bytes)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_clock)
}

void TTSimCommunicator::close_simulator_binary() {
    if (copied_simulator_fd_ != -1) {
        close(copied_simulator_fd_);
        copied_simulator_fd_ = -1;
    }
}

}  // namespace tt::umd
