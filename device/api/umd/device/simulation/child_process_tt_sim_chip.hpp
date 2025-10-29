/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <filesystem>
#include <memory>

#include "umd/device/chip/chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/simulation/message_data.hpp"

namespace tt::umd {

// ChildProcessTTSimChip runs in the child process and handles the .so file interaction
class ChildProcessTTSimChip {
public:
    ChildProcessTTSimChip(ChipId chip_id, const std::filesystem::path& simulator_directory,
                ClusterDescriptor* cluster_desc,
                int parent_to_child_fd, int child_to_parent_fd);
    ~ChildProcessTTSimChip();

    // Main loop for the child process
    int run();

    // Message handlers
    void handle_start_device();
    void handle_close_device();
    void handle_write_to_device(const void* data, uint32_t data_size);
    std::vector<uint8_t> handle_read_from_device(const void* data, uint32_t data_size);
    void handle_send_tensix_risc_reset(const void* data, uint32_t data_size);
    void handle_assert_risc_reset(const void* data, uint32_t data_size);
    void handle_deassert_risc_reset(const void* data, uint32_t data_size);
    bool handle_connect_eth_sockets();

private:
    ChipId chip_id_;
    std::filesystem::path simulator_directory_;
    ClusterDescriptor* cluster_desc_;

    // Pipe file descriptors for communication
    int parent_to_child_fd_;
    int child_to_parent_fd_;

    bool device_started_;
    bool should_exit_;

    // .so file handle and function pointers
    void* libttsim_handle_;
    std::filesystem::path copied_simulator_directory_;
    uint32_t libttsim_pci_device_id_;

    // Function pointers from the .so file
    void (*pfn_libttsim_configure_eth_socket)(uint32_t tile_id, const char *address, bool is_server);
    bool (*pfn_libttsim_connect_eth_sockets)();
    void (*pfn_libttsim_init)(uint32_t chip_id);
    void (*pfn_libttsim_exit)();
    uint32_t (*pfn_libttsim_pci_config_rd32)(uint32_t bus_device_function, uint32_t offset);
    void (*pfn_libttsim_tile_rd_bytes)(uint32_t x, uint32_t y, uint64_t addr, void* p, uint32_t size);
    void (*pfn_libttsim_tile_wr_bytes)(uint32_t x, uint32_t y, uint64_t addr, const void* p, uint32_t size);
    void (*pfn_libttsim_clock)(uint32_t n_clocks);

    // Architecture implementation
    std::unique_ptr<architecture_implementation> architecture_impl_;

    // Helper methods
    bool load_simulator_library();
    void unload_simulator_library();
    void setup_ethernet_connections();
    void send_response(bool success = true, const void* data = nullptr, uint32_t data_size = 0);

    // Message reading helpers
    bool read_message(Message& msg, std::vector<uint8_t>& data_buffer);
    void process_message(const Message& msg, const std::vector<uint8_t>& data_buffer);
};

// Main function for child process
int child_process_main(int parent_to_child_fd, int child_to_parent_fd, ChipId chip_id, const std::filesystem::path& simulator_directory, ClusterDescriptor* cluster_desc);

}  // namespace tt::umd
