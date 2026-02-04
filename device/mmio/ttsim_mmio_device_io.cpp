// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/mmio/ttsim_mmio_device_io.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace tt::umd {

TTSimMMIODeviceIO::TTSimMMIODeviceIO(
    const std::filesystem::path& simulator_directory,
    const SocDescriptor& soc_descriptor,
    size_t size,
    uint64_t base_address,
    const tlb_data& config)
    : simulator_directory_(simulator_directory),
      soc_descriptor_(soc_descriptor),
      base_address_(base_address),
      config_(config),
      window_size_(size) {
    // Initialize simulator if directory is provided
    if (!simulator_directory_.empty()) {
        initialize_simulator();
    }
}

TTSimMMIODeviceIO::~TTSimMMIODeviceIO() {
    close_simulator();
}

void TTSimMMIODeviceIO::write32(uint64_t offset, uint32_t value) {
    validate(offset, sizeof(uint32_t));

    if (!simulator_initialized_) {
        throw std::runtime_error("TTSim simulator not initialized");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    uint64_t paddr = base_address_ + offset;

    if (pfn_libttsim_pci_mem_wr_bytes) {
        pfn_libttsim_pci_mem_wr_bytes(paddr, &value, sizeof(uint32_t));
    } else {
        throw std::runtime_error("TTSim PCI memory write function not available");
    }
}

uint32_t TTSimMMIODeviceIO::read32(uint64_t offset) {
    validate(offset, sizeof(uint32_t));

    if (!simulator_initialized_) {
        throw std::runtime_error("TTSim simulator not initialized");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    uint64_t paddr = base_address_ + offset;
    uint32_t value = 0;

    if (pfn_libttsim_pci_mem_rd_bytes) {
        pfn_libttsim_pci_mem_rd_bytes(paddr, &value, sizeof(uint32_t));
    } else {
        throw std::runtime_error("TTSim PCI memory read function not available");
    }

    return value;
}

void TTSimMMIODeviceIO::write_register(uint64_t offset, const void* data, size_t size) {
    validate(offset, size);

    if (!simulator_initialized_) {
        throw std::runtime_error("TTSim simulator not initialized");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    uint64_t paddr = base_address_ + offset;

    if (pfn_libttsim_pci_mem_wr_bytes) {
        pfn_libttsim_pci_mem_wr_bytes(paddr, data, size);
    } else {
        throw std::runtime_error("TTSim PCI memory write function not available");
    }
}

void TTSimMMIODeviceIO::read_register(uint64_t offset, void* data, size_t size) {
    validate(offset, size);

    if (!simulator_initialized_) {
        throw std::runtime_error("TTSim simulator not initialized");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    uint64_t paddr = base_address_ + offset;

    if (pfn_libttsim_pci_mem_rd_bytes) {
        pfn_libttsim_pci_mem_rd_bytes(paddr, data, size);
    } else {
        throw std::runtime_error("TTSim PCI memory read function not available");
    }
}

void TTSimMMIODeviceIO::write_block(uint64_t offset, const void* data, size_t size) {
    write_register(offset, data, size);
}

void TTSimMMIODeviceIO::read_block(uint64_t offset, void* data, size_t size) {
    read_register(offset, data, size);
}

void TTSimMMIODeviceIO::read_block_reconfigure(
    void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    if (!simulator_initialized_) {
        throw std::runtime_error("TTSim simulator not initialized");
    }

    std::lock_guard<std::mutex> lock(device_lock_);

    if (pfn_libttsim_tile_rd_bytes) {
        pfn_libttsim_tile_rd_bytes(core.x, core.y, addr, mem_ptr, size);
    } else {
        throw std::runtime_error("TTSim tile read function not available");
    }
}

void TTSimMMIODeviceIO::write_block_reconfigure(
    const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size, uint64_t ordering) {
    if (!simulator_initialized_) {
        throw std::runtime_error("TTSim simulator not initialized");
    }

    std::lock_guard<std::mutex> lock(device_lock_);

    if (pfn_libttsim_tile_wr_bytes) {
        pfn_libttsim_tile_wr_bytes(core.x, core.y, addr, mem_ptr, size);
    } else {
        throw std::runtime_error("TTSim tile write function not available");
    }
}

void TTSimMMIODeviceIO::noc_multicast_write_reconfigure(
    void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint64_t ordering) {
    // In TTSim, multicast is simulated by writing to each core in the range
    for (uint32_t x = core_start.x; x <= core_end.x; ++x) {
        for (uint32_t y = core_start.y; y <= core_end.y; ++y) {
            write_block_reconfigure(dst, {x, y}, addr, size, ordering);
        }
    }
}

size_t TTSimMMIODeviceIO::get_size() const {
    return window_size_;
}

void TTSimMMIODeviceIO::configure(const tlb_data& new_config) {
    std::lock_guard<std::mutex> lock(device_lock_);
    config_ = new_config;
    // In TTSim, configuration changes are stored but don't affect simulation behavior directly
}

uint64_t TTSimMMIODeviceIO::get_base_address() const {
    return base_address_;
}

void TTSimMMIODeviceIO::initialize_simulator() {
    if (simulator_initialized_) {
        return;
    }

    try {
        create_simulator_binary();
        load_simulator_library(simulator_directory_);

        if (pfn_libttsim_init) {
            pfn_libttsim_init();
            simulator_initialized_ = true;
        } else {
            throw std::runtime_error("TTSim init function not found");
        }
    } catch (const std::exception& e) {
        close_simulator();
        throw std::runtime_error("Failed to initialize TTSim: " + std::string(e.what()));
    }
}

void TTSimMMIODeviceIO::close_simulator() {
    if (simulator_initialized_ && pfn_libttsim_exit) {
        pfn_libttsim_exit();
        simulator_initialized_ = false;
    }

    if (libttsim_handle) {
        dlclose(libttsim_handle);
        libttsim_handle = nullptr;
    }

    close_simulator_binary();
}

void TTSimMMIODeviceIO::validate(uint64_t offset, size_t size) const {
    if (offset + size > window_size_) {
        throw std::runtime_error("TTSimMMIODeviceIO: Access beyond window boundary");
    }
}

void TTSimMMIODeviceIO::load_simulator_library(const std::filesystem::path& path) {
    std::filesystem::path lib_path = path / "libttsim.so";

    libttsim_handle = dlopen(lib_path.c_str(), RTLD_LAZY);
    if (!libttsim_handle) {
        throw std::runtime_error("Cannot load TTSim library: " + std::string(dlerror()));
    }

    // Load function pointers
    pfn_libttsim_init = reinterpret_cast<void (*)()>(dlsym(libttsim_handle, "libttsim_init"));
    pfn_libttsim_exit = reinterpret_cast<void (*)()>(dlsym(libttsim_handle, "libttsim_exit"));
    pfn_libttsim_pci_config_rd32 = reinterpret_cast<uint32_t (*)(uint32_t, uint32_t)>(
        dlsym(libttsim_handle, "libttsim_pci_config_rd32"));
    pfn_libttsim_pci_mem_rd_bytes = reinterpret_cast<void (*)(uint64_t, void*, uint32_t)>(
        dlsym(libttsim_handle, "libttsim_pci_mem_rd_bytes"));
    pfn_libttsim_pci_mem_wr_bytes = reinterpret_cast<void (*)(uint64_t, const void*, uint32_t)>(
        dlsym(libttsim_handle, "libttsim_pci_mem_wr_bytes"));
    pfn_libttsim_tile_rd_bytes = reinterpret_cast<void (*)(uint32_t, uint32_t, uint64_t, void*, uint32_t)>(
        dlsym(libttsim_handle, "libttsim_tile_rd_bytes"));
    pfn_libttsim_tile_wr_bytes = reinterpret_cast<void (*)(uint32_t, uint32_t, uint64_t, const void*, uint32_t)>(
        dlsym(libttsim_handle, "libttsim_tile_wr_bytes"));
    pfn_libttsim_clock = reinterpret_cast<void (*)(uint32_t)>(dlsym(libttsim_handle, "libttsim_clock"));
}

void TTSimMMIODeviceIO::create_simulator_binary() {
    // This is a simplified version - real implementation would handle binary creation
    // Similar to TTSimTTDevice::create_simulator_binary()
}

void TTSimMMIODeviceIO::close_simulator_binary() {
    if (copied_simulator_fd_ >= 0) {
        close(copied_simulator_fd_);
        copied_simulator_fd_ = -1;
    }
}

uint64_t TTSimMMIODeviceIO::translate_address_for_ttsim(tt_xy_pair core, uint64_t addr) const {
    // Simple address translation for TTSim
    return addr + (static_cast<uint64_t>(core.x) << 32) + (static_cast<uint64_t>(core.y) << 24);
}

}  // namespace tt::umd