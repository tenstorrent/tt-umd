/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "process_manager.hpp"

#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "message_data.hpp"

namespace tt::umd {

ProcessManager::ProcessManager(ChipId chip_id) :
    chip_id_(chip_id), child_running_(false), child_pid_(-1), parent_fd_(-1), child_fd_(-1) {}

ProcessManager::~ProcessManager() { stop_child_process(); }

void ProcessManager::create_sockets() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        TT_THROW("Failed to create socket pair: {}", strerror(errno));
    }
    parent_fd_ = fds[1];
    child_fd_ = fds[0];
}

void ProcessManager::close_fd() {
    if (parent_fd_ != -1) {
        close(parent_fd_);
        parent_fd_ = -1;
    }
}

void ProcessManager::start_child_process(
    const std::filesystem::path& simulator_directory, ClusterDescriptor* cluster_desc) {
    if (child_running_) {
        log_warning(tt::LogEmulationDriver, "Child process already running for chip {}", chip_id_);
        return;
    }

    create_sockets();

    // TODO: How to automatically package and get this executable path?
    // Currently this is a temp solution which requires copying the executable to the simulator directory
    std::filesystem::path child_process_executable = simulator_directory.parent_path() / "child_process_tt_sim_chip";
    if (!std::filesystem::exists(child_process_executable)) {
        TT_THROW("Child process executable not found at: {}", child_process_executable);
    }
    auto cluster_desc_file = cluster_desc->serialize_to_file();

    // Prepare command line arguments for the executable
    // Note: Arguments must match the expected format in child_process.cpp main()
    std::vector<std::string> args = {
        child_process_executable,      // argv[0] - executable path
        std::to_string(child_fd_),     // argv[1] - read fd
        std::to_string(child_fd_),     // argv[2] - write fd
        std::to_string(chip_id_),      // argv[3] - chip ID
        simulator_directory.string(),  // argv[4] - simulator directory
        cluster_desc_file.string()     // argv[5] - cluster descriptor file (mock)
    };

    // Convert to char* array for posix_spawn
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);  // null terminator

    // Set up file actions for posix_spawn
    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);

    // Close parent's end of the socket in child process
    posix_spawn_file_actions_addclose(&file_actions, parent_fd_);

    // Spawn the child process
    int result =
        posix_spawn(&child_pid_, child_process_executable.c_str(), &file_actions, nullptr, argv.data(), environ);

    posix_spawn_file_actions_destroy(&file_actions);

    close(child_fd_);

    if (result != 0) {
        close_fd();
        TT_THROW("Failed to spawn child process: {}", strerror(result));
    }

    child_running_ = true;
}

void ProcessManager::stop_child_process() {
    if (!child_running_) {
        return;
    }

    // Send exit message to child
    send_message_with_response(MessageType::EXIT, nullptr, 0, nullptr, 0);

    // Wait for child process to exit
    if (child_pid_ != -1) {
        int status;
        waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
    }

    close_fd();
    child_running_ = false;
}

void ProcessManager::send_message_with_response(
    MessageType type, const void* data, uint32_t data_size, void* response_data, uint32_t response_size) {
    if (!child_running_) {
        TT_THROW("Child process not running");
    }

    // Send message header
    Message msg;
    msg.type = type;
    msg.size = data_size;

    ssize_t bytes_written = safe_write(parent_fd_, &msg, sizeof(Message));
    if (bytes_written < 0) {
        TT_THROW("Failed to send message header to child process: {}", strerror(errno));
    }
    if (bytes_written != sizeof(Message)) {
        TT_THROW("Incomplete message header write: expected {}, got {}", sizeof(Message), bytes_written);
    }

    // Send data directly if present
    if (data && data_size > 0) {
        bytes_written = safe_write(parent_fd_, data, data_size);
        if (bytes_written < 0) {
            TT_THROW("Failed to send message data to child process: {}", strerror(errno));
        }
        if (bytes_written != static_cast<ssize_t>(data_size)) {
            TT_THROW("Incomplete message data write: expected {}, got {}", data_size, bytes_written);
        }
    }

    // Wait for response message
    Message response_msg;
    ssize_t bytes_read = safe_read(parent_fd_, &response_msg, sizeof(Message));
    if (bytes_read < 0) {
        TT_THROW("Failed to read response message: {}", strerror(errno));
    }
    if (bytes_read != sizeof(Message)) {
        TT_THROW("Incomplete response message read: expected {}, got {}", sizeof(Message), bytes_read);
    }

    if (response_msg.type != MessageType::RESPONSE) {
        TT_THROW("Invalid response message");
    }

    // Validate response size matches expected size
    if (response_data && response_size > 0) {
        if (response_msg.size != response_size) {
            TT_THROW("Response size mismatch: expected {}, got {}", response_size, response_msg.size);
        }
    } else if (response_msg.size != 0) {
        TT_THROW("Unexpected response data: expected 0 bytes, got {}", response_msg.size);
    }

    // Read response data if present
    if (response_data && response_size > 0) {
        bytes_read = safe_read(parent_fd_, response_data, response_size);
        if (bytes_read < 0) {
            TT_THROW("Failed to read response data: {}", strerror(errno));
        }
        if (bytes_read != static_cast<ssize_t>(response_size)) {
            TT_THROW("Incomplete response data read: expected {}, got {}", response_size, bytes_read);
        }
    }
}

void ProcessManager::send_message_with_data_and_response(
    MessageType type, const void* header_data, uint32_t header_size, const void* payload_data, uint32_t payload_size) {
    if (!child_running_) {
        TT_THROW("Child process not running");
    }

    uint32_t total_data_size = header_size + payload_size;

    // Send message header
    Message msg;
    msg.type = type;
    msg.size = total_data_size;

    ssize_t bytes_written = safe_write(parent_fd_, &msg, sizeof(Message));
    if (bytes_written < 0) {
        TT_THROW("Failed to send message header to child process: {}", strerror(errno));
    }
    if (bytes_written != sizeof(Message)) {
        TT_THROW("Incomplete message header write: expected {}, got {}", sizeof(Message), bytes_written);
    }

    // Send header data
    if (header_size > 0) {
        bytes_written = safe_write(parent_fd_, header_data, header_size);
        if (bytes_written < 0) {
            TT_THROW("Failed to send header data to child process: {}", strerror(errno));
        }
        if (bytes_written != static_cast<ssize_t>(header_size)) {
            TT_THROW("Incomplete header data write: expected {}, got {}", header_size, bytes_written);
        }
    }

    // Send payload data directly
    if (payload_size > 0) {
        bytes_written = safe_write(parent_fd_, payload_data, payload_size);
        if (bytes_written < 0) {
            TT_THROW("Failed to send payload data to child process: {}", strerror(errno));
        }
        if (bytes_written != static_cast<ssize_t>(payload_size)) {
            TT_THROW("Incomplete payload data write: expected {}, got {}", payload_size, bytes_written);
        }
    }

    // Wait for response message
    Message response_msg;
    ssize_t bytes_read = safe_read(parent_fd_, &response_msg, sizeof(Message));
    if (bytes_read < 0) {
        TT_THROW("Failed to read response message: {}", strerror(errno));
    }
    if (bytes_read != sizeof(Message)) {
        TT_THROW("Incomplete response message read: expected {}, got {}", sizeof(Message), bytes_read);
    }

    if (response_msg.type != MessageType::RESPONSE) {
        TT_THROW("Invalid response message");
    }
}

}  // namespace tt::umd
