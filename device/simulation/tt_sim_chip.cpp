// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/tt_sim_chip.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

// NOLINTBEGIN.
#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find symbol: ", #func_name, dlerror());                  \
    }

// NOLINTEND.
namespace tt::umd {

static_assert(!std::is_abstract<TTSimChip>(), "TTSimChip must be non-abstract.");

TTSimChip::TTSimChip(
    const std::filesystem::path& simulator_directory,
    SocDescriptor soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary,
    int num_host_mem_channels) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id, num_host_mem_channels),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)) {
    if (copy_sim_binary) {
        create_simulator_binary();
        copy_simulator_binary();
        secure_simulator_binary();
        load_simulator_library(fmt::format("/proc/self/fd/{}", copied_simulator_fd_));
    } else {
        load_simulator_library(simulator_directory_.string());
    }
}

TTSimChip::~TTSimChip() {
    dlclose(libttsim_handle);
    close_simulator_binary();
}

void TTSimChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    pfn_libttsim_init();

    // Read the PCI ID (first 32 bits of PCI config space).
    uint32_t pci_id = pfn_libttsim_pci_config_rd32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = pci_id >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");
}

void TTSimChip::close_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    pfn_libttsim_exit();
}

void TTSimChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, l1_dest, src, size);
}

void TTSimChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, l1_src, dest, size);
    pfn_libttsim_clock(10);
}

void TTSimChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    if ((libttsim_pci_device_id == 0x401E) || (libttsim_pci_device_id == 0xB140)) {  // WH/BH
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint32_t reset_value = uint32_t(soft_resets);
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint64_t reset_value = uint64_t(soft_resets);
        if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
            reset_value = 0xF0000;  // This is using old API, translate to QSR values
        } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
            reset_value = 0xFFF00;  // This is using old API, translate to QSR values
        }
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        TT_THROW("Missing implementation of reset for this chip.");
    }
}

void TTSimChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    Chip::send_tensix_risc_reset(soft_resets);
}

void TTSimChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value &=
            ~(uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        pfn_libttsim_tile_wr_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value |= soft_reset_update;
        pfn_libttsim_tile_wr_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    }
}

void TTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value |=
            (uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        pfn_libttsim_tile_wr_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value &= ~soft_reset_update;
        pfn_libttsim_tile_wr_bytes(
            translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    }
}

void TTSimChip::create_simulator_binary() {
    const std::string filename = simulator_directory_.stem().string();
    const std::string extension = simulator_directory_.extension().string();
    const std::string memfd_name = (filename + "_chip" + std::to_string(chip_id_) + extension);
    copied_simulator_fd_ = memfd_create(memfd_name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (copied_simulator_fd_ < 0) {
        TT_THROW("Failed to create memfd: {}", strerror(errno));
    }
}

off_t TTSimChip::resize_simulator_binary(int src_fd) {
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

void TTSimChip::copy_simulator_binary() {
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

void TTSimChip::secure_simulator_binary() {
    if (fcntl(copied_simulator_fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        close_simulator_binary();
        TT_THROW("Failed to seal memfd: {}", strerror(errno));
    }
}

void TTSimChip::load_simulator_library(const std::filesystem::path& path) {
    libttsim_handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!libttsim_handle) {
        close_simulator_binary();
        TT_THROW("Failed to dlopen simulator library: {}", dlerror());
    }
    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_pci_config_rd32)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_clock)
}

void TTSimChip::close_simulator_binary() {
    if (copied_simulator_fd_ != -1) {
        close(copied_simulator_fd_);
        copied_simulator_fd_ = -1;
    }
}

}  // namespace tt::umd
