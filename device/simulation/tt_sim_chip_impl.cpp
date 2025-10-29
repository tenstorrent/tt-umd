/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tt_sim_chip_impl.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <tuple>

#include "assert.hpp"

// NOLINTBEGIN
#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find symbol: ", #func_name, dlerror());                  \
    }

// NOLINTEND

namespace tt::umd {

static_assert(!std::is_abstract<TTSimChipImpl>(), "TTSimChipImpl must be non-abstract.");

TTSimChipImpl::TTSimChipImpl(
    const std::filesystem::path& simulator_directory,
    ClusterDescriptor* cluster_desc,
    ChipId chip_id,
    bool duplicate_simulator_directory) :
    chip_id_(chip_id),
    cluster_desc_(cluster_desc),
    architecture_impl_(architecture_implementation::create(cluster_desc_->get_arch(chip_id_))),
    simulator_directory_(simulator_directory) {
    if (!std::filesystem::exists(simulator_directory)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory);
    }
    std::filesystem::path sim_dir;
    // Create a unique copy of the .so file with chip_id appended to allow multiple instances
    if (duplicate_simulator_directory) {
        create_simulator_binary();
        copy_simulator_binary();
        secure_simulator_binary();
        sim_dir = fmt::format("/proc/self/fd/{}", copied_simulator_fd_);
    } else {
        sim_dir = simulator_directory;
    }
    load_simulator_library(sim_dir);
    setup_ethernet_connections();
}

void TTSimChipImpl::setup_ethernet_connections() {
    auto get_remote_address = [](uint64_t unique_chip_id,
                                 EthernetChannel channel,
                                 uint64_t remote_chip_id,
                                 EthernetChannel remote_channel) -> std::tuple<std::string, bool> {
        // TODO: We need to uniquify the directory per test to avoid collisions
        // Currently this will only work for one test per host (the test could be simulating multi-host scenarios)
        // but separate individual tests could conflict

        // Create a deterministic ordering: smaller chip_id first, then smaller channel
        bool is_server =
            (unique_chip_id < remote_chip_id) || (unique_chip_id == remote_chip_id && channel < remote_channel);

        if (is_server) {
            return {fmt::format("{}_{}_{}_{}", unique_chip_id, channel, remote_chip_id, remote_channel), true};
        } else {
            return {fmt::format("{}_{}_{}_{}", remote_chip_id, remote_channel, unique_chip_id, channel), false};
        }
    };
    if (cluster_desc_->get_ethernet_connections().find(chip_id_) != cluster_desc_->get_ethernet_connections().end()) {
        auto unique_chip_id = cluster_desc_->get_chip_unique_ids().at(chip_id_);
        for (const auto& [channel, remote_chip_channel] : cluster_desc_->get_ethernet_connections().at(chip_id_)) {
            auto remote_chip_id = cluster_desc_->get_chip_unique_ids().at(std::get<0>(remote_chip_channel));
            auto remote_channel = std::get<1>(remote_chip_channel);
            auto [remote_address, is_server] =
                get_remote_address(unique_chip_id, channel, remote_chip_id, remote_channel);
            eth_connections_[channel].create_socket(remote_address, true, is_server);
        }
    }
    if (cluster_desc_->get_ethernet_connections_to_remote_devices().find(chip_id_) !=
        cluster_desc_->get_ethernet_connections_to_remote_devices().end()) {
        auto unique_chip_id = cluster_desc_->get_chip_unique_ids().at(chip_id_);
        for (const auto& [channel, remote_chip_channel] :
             cluster_desc_->get_ethernet_connections_to_remote_devices().at(chip_id_)) {
            auto remote_chip_id = std::get<0>(remote_chip_channel);
            auto remote_channel = std::get<1>(remote_chip_channel);
            auto [remote_address, is_server] =
                get_remote_address(unique_chip_id, channel, remote_chip_id, remote_channel);
            eth_connections_[channel].create_socket(remote_address, true, is_server);
        }
    }
}

bool TTSimChipImpl::connect_eth_links() {
    bool result = true;
    for (auto& [channel, eth_connection] : eth_connections_) {
        if (eth_connection.is_connected()) {
            continue;
        }
        bool connected = eth_connection.connect();
        if (connected) {
            auto [write_fd, read_fd] = eth_connection.get_fds();
            pfn_libttsim_configure_eth_link(channel, write_fd, read_fd);
        } else {
            result = false;
        }
    }
    return result;
}

TTSimChipImpl::~TTSimChipImpl() {
    eth_connections_.clear();
    dlclose(libttsim_handle);
    close_simulator_binary();
}

void TTSimChipImpl::start_device() {
    pfn_libttsim_init();

    // Read the PCI ID (first 32 bits of PCI config space)
    uint32_t pci_id = pfn_libttsim_pci_config_rd32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = pci_id >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");
}

void TTSimChipImpl::close_device() {
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    pfn_libttsim_exit();
}

void TTSimChipImpl::write_to_device(tt_xy_pair translated_core, const void* src, uint64_t l1_dest, uint32_t size) {
    log_debug(
        tt::LogEmulationDriver,
        "Device writing {} bytes to l1_dest {} in core {}",
        size,
        l1_dest,
        translated_core.str());
    pfn_libttsim_tile_wr_bytes(translated_core.x, translated_core.y, l1_dest, src, size);
}

void TTSimChipImpl::read_from_device(tt_xy_pair translated_core, void* dest, uint64_t l1_src, uint32_t size) {
    pfn_libttsim_tile_rd_bytes(translated_core.x, translated_core.y, l1_src, dest, size);
}

void TTSimChipImpl::clock(uint32_t clock) { pfn_libttsim_clock(clock); }

void TTSimChipImpl::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
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

void TTSimChipImpl::assert_risc_reset(tt_xy_pair translated_core, const RiscType selected_riscs) {
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value &=
            ~(uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value |= soft_reset_update;
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    }
}

void TTSimChipImpl::deassert_risc_reset(
    tt_xy_pair translated_core, const RiscType selected_riscs, bool staggered_start) {
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    if (libttsim_pci_device_id == 0xFEED) {  // QSR
        uint64_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value |=
            (uint64_t)soft_reset_update;  // QSR logic is reversed for DM cores, so we need to invert the update.
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        uint32_t reset_value;
        pfn_libttsim_tile_rd_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
        reset_value &= ~soft_reset_update;
        pfn_libttsim_tile_wr_bytes(
            translated_core.x, translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    }
}

void TTSimChipImpl::create_simulator_binary() {
    const std::string filename = simulator_directory_.stem().string();
    const std::string extension = simulator_directory_.extension().string();
    const std::string memfd_name = (filename + "_chip" + std::to_string(chip_id_) + extension);
    copied_simulator_fd_ = memfd_create(memfd_name.c_str(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (copied_simulator_fd_ < 0) {
        TT_THROW("Failed to create memfd: {}", strerror(errno));
    }
}

off_t TTSimChipImpl::resize_simulator_binary(int src_fd) {
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

void TTSimChipImpl::copy_simulator_binary() {
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

void TTSimChipImpl::secure_simulator_binary() {
    if (fcntl(copied_simulator_fd_, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) < 0) {
        close_simulator_binary();
        TT_THROW("Failed to seal memfd: {}", strerror(errno));
    }
}

void TTSimChipImpl::load_simulator_library(const std::filesystem::path& sim_dir) {
    libttsim_handle = dlopen(sim_dir.c_str(), RTLD_LAZY);
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
    DLSYM_FUNCTION(libttsim_configure_eth_link)
}

void TTSimChipImpl::close_simulator_binary() {
    if (copied_simulator_fd_ != -1) {
        close(copied_simulator_fd_);
        copied_simulator_fd_ = -1;
    }
}

}  // namespace tt::umd
