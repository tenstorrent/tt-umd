// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_sim_tt_device.hpp"

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
#include "simulation_device_generated.h"
#include "umd/device/simulation/simulation_chip.hpp"

// NOLINTBEGIN.
#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find symbol: ", #func_name, dlerror());                  \
    }

// NOLINTEND.

namespace tt::umd {

static_assert(!std::is_abstract<TTSimTTDevice>(), "TTSimChip must be non-abstract.");

std::unique_ptr<TTSimTTDevice> TTSimTTDevice::create(const std::filesystem::path& simulator_directory) {
    auto soc_desc_path = SimulationChip::get_soc_descriptor_path_from_simulator_path(simulator_directory);
    SocDescriptor soc_descriptor = SocDescriptor(soc_desc_path);
    return std::make_unique<TTSimTTDevice>(simulator_directory, soc_descriptor, 0);
}

TTSimTTDevice::TTSimTTDevice(
    const std::filesystem::path& simulator_directory,
    SocDescriptor soc_descriptor,
    ChipId chip_id,
    bool copy_sim_binary) :
    simulator_directory_(simulator_directory),
    soc_descriptor_(std::move(soc_descriptor)),
    chip_id_(chip_id),
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

TTSimTTDevice::~TTSimTTDevice() {
    dlclose(libttsim_handle);
    close_simulator_binary();
}

void TTSimTTDevice::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    pfn_libttsim_init();

    // Read the PCI ID (first 32 bits of PCI config space).
    uint32_t pci_id = pfn_libttsim_pci_config_rd32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = pci_id >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");
    if (libttsim_pci_device_id == 0x401E) {  // WH: use 16MiB TLB regions
        tlb_region_size = 0x1000000;
    } else if (libttsim_pci_device_id == 0xB140) {  // BH: use 2MiB TLB regions
        tlb_region_size = 0x200000;
    }
}

void TTSimTTDevice::close_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    pfn_libttsim_exit();
}

void TTSimTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogUMD, "Device writing {} bytes to l1_dest {} in core {}", size, addr, core.str());
    if (tlb_region_size) {  // if set, split into requests that do not span TLB regions
        while (size) {
            uint32_t cur_size = std::min(size, tlb_region_size - uint32_t(addr & (tlb_region_size - 1)));
            pfn_libttsim_tile_wr_bytes(core.x, core.y, addr, mem_ptr, cur_size);
            addr += cur_size;
            mem_ptr = reinterpret_cast<const uint8_t*>(mem_ptr) + cur_size;
            size -= cur_size;
        }
    } else {
        pfn_libttsim_tile_wr_bytes(core.x, core.y, addr, mem_ptr, size);
    }
}

void TTSimTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    if (tlb_region_size) {  // if set, split into requests that do not span TLB regions
        while (size) {
            uint32_t cur_size = std::min(size, tlb_region_size - uint32_t(addr & (tlb_region_size - 1)));
            pfn_libttsim_tile_rd_bytes(core.x, core.y, addr, mem_ptr, cur_size);
            addr += cur_size;
            mem_ptr = reinterpret_cast<uint8_t*>(mem_ptr) + cur_size;
            size -= cur_size;
        }
    } else {
        pfn_libttsim_tile_rd_bytes(core.x, core.y, addr, mem_ptr, size);
    }
    pfn_libttsim_clock(10);
}

void TTSimTTDevice::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
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

void TTSimTTDevice::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    for (const tt_xy_pair core : soc_descriptor_.get_cores(CoreType::TENSIX)) {
        send_tensix_risc_reset(core, soft_resets);
    }
}

void TTSimTTDevice::assert_risc_reset(tt_xy_pair core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        pfn_libttsim_tile_rd_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value &=
            ~(uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        pfn_libttsim_tile_wr_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        pfn_libttsim_tile_rd_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value |= soft_reset_update;
        pfn_libttsim_tile_wr_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    }
}

void TTSimTTDevice::deassert_risc_reset(tt_xy_pair core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        pfn_libttsim_tile_rd_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value |=
            (uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        pfn_libttsim_tile_wr_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        pfn_libttsim_tile_rd_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value &= ~soft_reset_update;
        pfn_libttsim_tile_wr_bytes(core.x, core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    }
}

void TTSimTTDevice::dma_d2h(void* dst, uint32_t src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d(uint32_t dst, const void* src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    throw std::runtime_error("DMA operations are not supported in TTSim simulation device.");
}

void TTSimTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC APB access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC APB access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC CSM access is not supported in TTSim simulation device.");
}

void TTSimTTDevice::write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    throw std::runtime_error("ARC CSM access is not supported in TTSim simulation device.");
}

bool TTSimTTDevice::wait_arc_core_start(const std::chrono::milliseconds timeout_ms) {
    throw std::runtime_error("Waiting for ARC core start is not supported in TTSim simulation device.");
}

std::chrono::milliseconds TTSimTTDevice::wait_eth_core_training(
    const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms) {
    throw std::runtime_error("Waiting for ETH core training is not supported in TTSim simulation device.");
}

uint32_t TTSimTTDevice::get_clock() {
    throw std::runtime_error("Getting clock is not supported in TTSim simulation device.");
}

uint32_t TTSimTTDevice::get_min_clock_freq() {
    throw std::runtime_error("Getting minimum clock frequency is not supported in TTSim simulation device.");
}

bool TTSimTTDevice::get_noc_translation_enabled() {
    throw std::runtime_error("Getting NOC translation status is not supported in TTSim simulation device.");
}

void TTSimTTDevice::create_simulator_binary() {
    const std::string filename = simulator_directory_.stem().string();
    const std::string extension = simulator_directory_.extension().string();
    const std::string memfd_name = (filename + "_chip" + std::to_string(chip_id_) + extension);
    copied_simulator_fd_ = memfd_create(memfd_name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (copied_simulator_fd_ < 0) {
        TT_THROW("Failed to create memfd: {}", strerror(errno));
    }
}

off_t TTSimTTDevice::resize_simulator_binary(int src_fd) {
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

void TTSimTTDevice::copy_simulator_binary() {
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

void TTSimTTDevice::secure_simulator_binary() {
    if (fcntl(copied_simulator_fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        close_simulator_binary();
        TT_THROW("Failed to seal memfd: {}", strerror(errno));
    }
}

void TTSimTTDevice::load_simulator_library(const std::filesystem::path& path) {
    libttsim_handle = dlopen(path.c_str(), RTLD_LAZY);
    if (!libttsim_handle) {
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

void TTSimTTDevice::close_simulator_binary() {
    if (copied_simulator_fd_ != -1) {
        close(copied_simulator_fd_);
        copied_simulator_fd_ = -1;
    }
}

}  // namespace tt::umd
