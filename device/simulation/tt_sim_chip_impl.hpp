/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "eth_connection.hpp"
#include "umd/device/cluster.hpp"

namespace tt::umd {

// TTSIM implementation using dynamic library (.so files).
class TTSimChipImpl {
public:
    TTSimChipImpl(
        const std::filesystem::path& simulator_directory,
        ClusterDescriptor* cluster_desc,
        ChipId chip_id,
        bool duplicate_simulator_directory);
    ~TTSimChipImpl();

    void start_device();
    void close_device();

    void write_to_device(tt_xy_pair translated_core, const void* src, uint64_t l1_dest, uint32_t size);
    void read_from_device(tt_xy_pair translated_core, void* dest, uint64_t l1_src, uint32_t size);

    void clock(uint32_t clock);

    void send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets);
    void assert_risc_reset(tt_xy_pair translated_core, const RiscType selected_riscs);
    void deassert_risc_reset(tt_xy_pair translated_core, const RiscType selected_riscs, bool staggered_start);
    bool connect_eth_links();

private:
    void create_simulator_binary();
    off_t resize_simulator_binary(int src_fd);
    void copy_simulator_binary();
    void secure_simulator_binary();
    void close_simulator_binary();
    void load_simulator_library(const std::filesystem::path& sim_dir);
    int copied_simulator_fd_ = -1;
    ChipId chip_id_;
    ClusterDescriptor* cluster_desc_;
    std::unique_ptr<architecture_implementation> architecture_impl_;
    std::filesystem::path simulator_directory_;
    std::unordered_map<EthernetChannel, EthConnection> eth_connections_;

    void setup_ethernet_connections();

    void* libttsim_handle = nullptr;
    uint32_t libttsim_pci_device_id = 0;
    void (*pfn_libttsim_configure_eth_link)(uint32_t tile_id, int write_fd, int read_fd) = nullptr;
    void (*pfn_libttsim_init)() = nullptr;
    void (*pfn_libttsim_exit)() = nullptr;
    uint32_t (*pfn_libttsim_pci_config_rd32)(uint32_t bus_device_function, uint32_t offset) = nullptr;
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void* p, uint32_t size) = nullptr;
    void (*pfn_libttsim_clock)(uint32_t n_clocks) = nullptr;
};

}  // namespace tt::umd
