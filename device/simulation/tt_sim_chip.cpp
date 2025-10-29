/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/tt_sim_chip.hpp"

#include <dlfcn.h>

#include <filesystem>
#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "umd/device/driver_atomics.hpp"

#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find symbol: ", #func_name, dlerror());                  \
    }

namespace tt::umd {

static_assert(!std::is_abstract<TTSimChip>(), "TTSimChip must be non-abstract.");

TTSimChip::TTSimChip(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ClusterDescriptor* cluster_desc, ChipId chip_id, std::unordered_map<ChipId, std::unique_ptr<Chip>> * chips_to_clock) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id),
    architecture_impl_(architecture_implementation::create(soc_descriptor_.arch)),
    chips_to_clock_(chips_to_clock) {
    if (!std::filesystem::exists(simulator_directory)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory);
    }

    // Create a unique copy of the .so file with chip_id appended to allow multiple instances
    const auto sim_chip_dir_template = std::filesystem::temp_directory_path() / "umd_XXXXXX";
    const std::filesystem::path sim_chip_dir = mkdtemp(sim_chip_dir_template.string().data());
    const std::string filename = simulator_directory.stem().string();
    const std::string extension = simulator_directory.extension().string();

    copied_simulator_directory_ = sim_chip_dir / (filename + "_chip" + std::to_string(chip_id) + extension);

    // Check if the copied .so file already exists and log a warning
    if (std::filesystem::exists(copied_simulator_directory_)) {
        log_warning(
            tt::LogEmulationDriver,
            "Copied simulator file already exists, overwriting: {}",
            copied_simulator_directory_.string());
    }

    // Copy the original .so file to the new location
    std::filesystem::copy_file(
        simulator_directory, copied_simulator_directory_, std::filesystem::copy_options::overwrite_existing);

    // dlopen the copied simulator library and dlsym the entry points.
    libttsim_handle = dlopen(copied_simulator_directory_.string().c_str(), RTLD_LAZY);
    if (!libttsim_handle) {
        TT_THROW("Failed to dlopen simulator library: ", dlerror());
    }
    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_configure_eth_socket)
    DLSYM_FUNCTION(libttsim_connect_eth_sockets)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_pci_config_rd32)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_clock)

    std::filesystem::create_directories("/tmp/umd/tt_sim_chip");
    auto get_remote_address = [](ChipId unique_chip_id, EthernetChannel channel, ChipId remote_chip_id, EthernetChannel remote_channel) -> std::tuple<std::string, bool> {
        // TODO: We need to uniquify the directory per test to avoid collisions
        // Currently this will only work for one test per host if there are connections with same identifiers.

        // Create a deterministic ordering: smaller chip_id first, then smaller channel
        bool is_server = (unique_chip_id < remote_chip_id) ||
                        (unique_chip_id == remote_chip_id && channel < remote_channel);

        if (is_server) {
            return {fmt::format("/tmp/umd/tt_sim_chip/{}_{}_{}_{}", unique_chip_id, channel, remote_chip_id, remote_channel), true};
        } else {
            return {fmt::format("/tmp/umd/tt_sim_chip/{}_{}_{}_{}", remote_chip_id, remote_channel, unique_chip_id, channel), false};
        }
    };
    if (cluster_desc->get_ethernet_connections().find(chip_id) != cluster_desc->get_ethernet_connections().end()) {
        auto unique_chip_id = cluster_desc->get_chip_unique_ids().at(chip_id);
        for (const auto& [channel, remote_chip_channel] : cluster_desc->get_ethernet_connections().at(chip_id)) {
            auto remote_chip_id = cluster_desc->get_chip_unique_ids().at(std::get<0>(remote_chip_channel));
            auto remote_channel = std::get<1>(remote_chip_channel);
            auto [remote_address, is_server] = get_remote_address(unique_chip_id, channel, remote_chip_id, remote_channel);
            pfn_libttsim_configure_eth_socket(channel, remote_address.c_str(), is_server);
        }
    }
    if (cluster_desc->get_ethernet_connections_to_remote_devices().find(chip_id) != cluster_desc->get_ethernet_connections_to_remote_devices().end()) {
        auto unique_chip_id = cluster_desc->get_chip_unique_ids().at(chip_id);
        for (const auto& [channel, remote_chip_channel] : cluster_desc->get_ethernet_connections_to_remote_devices().at(chip_id)) {
            auto remote_chip_id = std::get<0>(remote_chip_channel);
            auto remote_channel = std::get<1>(remote_chip_channel);
            auto [remote_address, is_server] = get_remote_address(unique_chip_id, channel, remote_chip_id, remote_channel);
            pfn_libttsim_configure_eth_socket(channel, remote_address.c_str(), is_server);
        }
    }
}

bool TTSimChip::connect_eth_sockets() {
    return pfn_libttsim_connect_eth_sockets();
}

TTSimChip::~TTSimChip() {
    dlclose(libttsim_handle);
    // Clean up the copied .so file
    if (!copied_simulator_directory_.empty() && std::filesystem::exists(copied_simulator_directory_)) {
        std::filesystem::remove_all(copied_simulator_directory_.parent_path());
    }
}

void TTSimChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    pfn_libttsim_init(*this->target_devices_in_cluster.begin());

    // Read the PCI ID (first 32 bits of PCI config space)
    uint32_t pci_id = pfn_libttsim_pci_config_rd32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id = pci_id >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");
}

void TTSimChip::close_device() {
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
    {
        std::lock_guard<std::mutex> lock(device_lock);
        tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
        pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, l1_src, dest, size);
    }
    for (uint32_t i = 0; i < 10; i++) {
        for (const auto& [chip_id, chip] : *chips_to_clock_) {
            static_cast<TTSimChip*>(chip.get())->clock(1);
        }
    }
}

void TTSimChip::clock(uint32_t clock) {
    std::lock_guard<std::mutex> lock(device_lock);
    pfn_libttsim_clock(clock);
}

void TTSimChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    if ((libttsim_pci_device_id == 0x401E) || (libttsim_pci_device_id == 0xB140)) {  // WH/BH
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint32_t reset_value = uint32_t(soft_resets);
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
    uint32_t reset_value;
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    reset_value |= soft_reset_update;
    pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
}

void TTSimChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(selected_riscs);
    uint32_t reset_value;
    pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    reset_value &= ~soft_reset_update;
    pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
}

}  // namespace tt::umd
