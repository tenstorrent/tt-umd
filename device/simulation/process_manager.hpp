/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "message_data.hpp"
#include "umd/device/chip/chip.hpp"
#include "umd/device/cluster.hpp"

namespace tt::umd {

// ProcessManager handles communication between parent and child processes
class ProcessManager {
public:
    ProcessManager(ChipId chip_id);
    ~ProcessManager();

    // Start the child process
    void start_child_process(const std::filesystem::path& simulator_directory, ClusterDescriptor* cluster_desc);

    // Stop the child process
    void stop_child_process();

    // Send a message and wait for response (blocking)
    void send_message_with_response(
        MessageType type, const void* data, uint32_t data_size, void* response_data, uint32_t response_size);

    // Send a message with separate header and data to avoid copying (blocking)
    void send_message_with_data_and_response(
        MessageType type,
        const void* header_data,
        uint32_t header_size,
        const void* payload_data,
        uint32_t payload_size);

    // Check if child process is running
    bool is_child_running() const { return child_running_; }

    // Get the chip ID
    ChipId get_chip_id() const { return chip_id_; }

private:
    ChipId chip_id_;
    bool child_running_;

    // Process management
    pid_t child_pid_;

    // Communication socket pair (bidirectional)
    int parent_fd_;
    int child_fd_;

    // Helper methods
    void create_sockets();
    void close_fd();
    void cleanup_child_process();
};

}  // namespace tt::umd
