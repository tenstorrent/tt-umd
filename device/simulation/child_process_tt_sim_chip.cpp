/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <poll.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "message_data.hpp"
#include "tt_sim_chip_impl.hpp"
#include "umd/device/chip/chip.hpp"
#include "umd/device/cluster.hpp"

namespace tt::umd {

// ChildProcessTTSimChip runs in the child process and handles the .so file interaction
class ChildProcessTTSimChip {
public:
    ChildProcessTTSimChip(
        ChipId chip_id,
        const std::filesystem::path& simulator_directory,
        ClusterDescriptor* cluster_desc,
        int read_fd,
        int write_fd);
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
    bool handle_connect_eth_links();

private:
    std::unique_ptr<TTSimChipImpl> impl_;

    // Communication socket file descriptor (bidirectional)
    int read_fd_;
    int write_fd_;

    bool device_started_;
    bool should_exit_;

    // Helper methods
    void send_response(bool success = true, const void* data = nullptr, uint32_t data_size = 0);

    // Message reading helpers
    bool read_message(Message& msg, std::vector<uint8_t>& data_buffer);
    void process_message(const Message& msg, const std::vector<uint8_t>& data_buffer);
};

ChildProcessTTSimChip::ChildProcessTTSimChip(
    ChipId chip_id,
    const std::filesystem::path& simulator_directory,
    ClusterDescriptor* cluster_desc,
    int read_fd,
    int write_fd) :
    impl_(std::make_unique<TTSimChipImpl>(simulator_directory, cluster_desc, chip_id, false)),
    read_fd_(read_fd),
    write_fd_(write_fd),
    device_started_(false),
    should_exit_(false) {}

ChildProcessTTSimChip::~ChildProcessTTSimChip() {
    should_exit_ = true;
    impl_.reset();
    if (read_fd_ != -1) {
        close(read_fd_);
    }
    if (write_fd_ != read_fd_ && write_fd_ != -1) {
        close(write_fd_);
    }
    read_fd_ = -1;
    write_fd_ = -1;
}

int ChildProcessTTSimChip::run() {
    // Main message processing loop
    while (!should_exit_) {
        Message msg;
        std::vector<uint8_t> data_buffer;

        if (read_message(msg, data_buffer)) {
            process_message(msg, data_buffer);
        } else if (should_exit_) {
            break;
        }

        // Clock the device if it's started (continuous clocking)
        if (device_started_) {
            impl_->clock(10);
        }
    }
    return 0;
}

bool ChildProcessTTSimChip::read_message(Message& msg, std::vector<uint8_t>& data_buffer) {
    // Check if data is available without blocking
    struct pollfd pfd;
    pfd.fd = read_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int result = poll(&pfd, 1, 0);
    // No data available
    if (result == 0) {
        return false;
    } else if (result < 0) {
        TT_THROW("Failed to check for available data: {}", strerror(errno));
    }

    // Parent process closed its end
    if (pfd.revents & POLLHUP) {
        should_exit_ = true;
        return false;
    }

    // Data is available, read message header
    ssize_t bytes_read = safe_read(read_fd_, &msg, sizeof(Message));
    if (bytes_read < 0) {
        TT_THROW("Failed to read message header: {}", strerror(errno));
    }
    if (bytes_read != sizeof(Message)) {
        TT_THROW("Incomplete message header read: expected {}, got {}", sizeof(Message), bytes_read);
    }

    // Read message data if present
    if (msg.size > 0) {
        data_buffer.resize(msg.size);
        bytes_read = safe_read(read_fd_, data_buffer.data(), msg.size);
        if (bytes_read < 0) {
            TT_THROW("Failed to read message data: {}", strerror(errno));
        }
        if (bytes_read != static_cast<ssize_t>(msg.size)) {
            TT_THROW("Incomplete message data read: expected {}, got {}", msg.size, bytes_read);
        }
    }

    return true;
}

void ChildProcessTTSimChip::process_message(const Message& msg, const std::vector<uint8_t>& data_buffer) {
    switch (msg.type) {
        case MessageType::START_DEVICE:
            handle_start_device();
            send_response();
            break;

        case MessageType::CLOSE_DEVICE:
            handle_close_device();
            send_response();
            break;

        case MessageType::WRITE_TO_DEVICE:
            handle_write_to_device(data_buffer.data(), msg.size);
            send_response();
            break;

        case MessageType::READ_FROM_DEVICE: {
            std::vector<uint8_t> read_data = handle_read_from_device(data_buffer.data(), msg.size);
            send_response(true, read_data.data(), read_data.size());
        } break;

        case MessageType::SEND_TENSIX_RISC_RESET:
            handle_send_tensix_risc_reset(data_buffer.data(), msg.size);
            send_response();
            break;

        case MessageType::ASSERT_RISC_RESET:
            handle_assert_risc_reset(data_buffer.data(), msg.size);
            send_response();
            break;

        case MessageType::DEASSERT_RISC_RESET:
            handle_deassert_risc_reset(data_buffer.data(), msg.size);
            send_response();
            break;

        case MessageType::CONNECT_ETH_LINKS: {
            bool result = handle_connect_eth_links();
            send_response(true, &result, sizeof(bool));
        } break;

        case MessageType::EXIT:
            should_exit_ = true;
            send_response();
            break;

        default:
            TT_THROW("Unknown message type: {}", static_cast<uint32_t>(msg.type));
            send_response(false);
            break;
    }
}

void ChildProcessTTSimChip::send_response(bool success, const void* data, uint32_t data_size) {
    Message response;
    response.type = MessageType::RESPONSE;
    response.size = data_size;

    ssize_t bytes_written = safe_write(write_fd_, &response, sizeof(Message));
    if (bytes_written < 0) {
        TT_THROW("Failed to send response: {}", strerror(errno));
        return;
    }
    if (bytes_written != sizeof(Message)) {
        TT_THROW("Incomplete response header write: expected {}, got {}", sizeof(Message), bytes_written);
        return;
    }

    // Send data if provided
    if (data && data_size > 0) {
        bytes_written = safe_write(write_fd_, data, data_size);
        if (bytes_written < 0) {
            TT_THROW("Failed to send response data: {}", strerror(errno));
            return;
        }
        if (bytes_written != static_cast<ssize_t>(data_size)) {
            TT_THROW("Incomplete response data write: expected {}, got {}", data_size, bytes_written);
            return;
        }
    }
}

void ChildProcessTTSimChip::handle_start_device() {
    impl_->start_device();
    device_started_ = true;
}

void ChildProcessTTSimChip::handle_close_device() {
    impl_->close_device();
    device_started_ = false;
}

void ChildProcessTTSimChip::handle_write_to_device(const void* data, uint32_t data_size) {
    if (data_size < sizeof(WriteMessageData)) {
        TT_THROW("Invalid data size for write message: {} < {}", data_size, sizeof(WriteMessageData));
    }
    const WriteMessageData* msg_data = static_cast<const WriteMessageData*>(data);
    uint32_t expected_size = sizeof(WriteMessageData) + msg_data->size;
    if (data_size != expected_size) {
        TT_THROW("Data size mismatch for write message: expected {}, got {}", expected_size, data_size);
    }
    const uint8_t* data_ptr = static_cast<const uint8_t*>(data) + sizeof(WriteMessageData);
    impl_->write_to_device(msg_data->translated_core, data_ptr, msg_data->l1_dest, msg_data->size);
}

std::vector<uint8_t> ChildProcessTTSimChip::handle_read_from_device(const void* data, uint32_t data_size) {
    if (data_size != sizeof(ReadMessageData)) {
        TT_THROW("Invalid data size for read message: expected {}, got {}", sizeof(ReadMessageData), data_size);
    }
    const ReadMessageData* msg_data = static_cast<const ReadMessageData*>(data);
    std::vector<uint8_t> read_buffer(msg_data->size);
    impl_->read_from_device(msg_data->translated_core, read_buffer.data(), msg_data->l1_src, msg_data->size);
    return read_buffer;
}

void ChildProcessTTSimChip::handle_send_tensix_risc_reset(const void* data, uint32_t data_size) {
    if (data_size != sizeof(TensixResetMessageData)) {
        TT_THROW(
            "Invalid data size for tensix reset message: expected {}, got {}",
            sizeof(TensixResetMessageData),
            data_size);
    }
    const TensixResetMessageData* msg_data = static_cast<const TensixResetMessageData*>(data);
    impl_->send_tensix_risc_reset(msg_data->translated_core, msg_data->soft_resets);
}

void ChildProcessTTSimChip::handle_assert_risc_reset(const void* data, uint32_t data_size) {
    if (data_size != sizeof(AssertResetMessageData)) {
        TT_THROW(
            "Invalid data size for assert reset message: expected {}, got {}",
            sizeof(AssertResetMessageData),
            data_size);
    }
    const AssertResetMessageData* msg_data = static_cast<const AssertResetMessageData*>(data);
    impl_->assert_risc_reset(msg_data->translated_core, msg_data->selected_riscs);
}

void ChildProcessTTSimChip::handle_deassert_risc_reset(const void* data, uint32_t data_size) {
    if (data_size != sizeof(DeassertResetMessageData)) {
        TT_THROW(
            "Invalid data size for deassert reset message: expected {}, got {}",
            sizeof(DeassertResetMessageData),
            data_size);
    }
    const DeassertResetMessageData* msg_data = static_cast<const DeassertResetMessageData*>(data);
    impl_->deassert_risc_reset(msg_data->translated_core, msg_data->selected_riscs, msg_data->staggered_start);
}

bool ChildProcessTTSimChip::handle_connect_eth_links() { return impl_->connect_eth_links(); }

int child_process_main(int argc, char* argv[]) {
    if (argc != 6) {
        TT_THROW("Usage: {} <read_fd> <write_fd> <chip_id> <simulator_directory> <cluster_descriptor_file>", argv[0]);
        return 1;
    }
    int read_fd = std::stoi(argv[1]);
    int write_fd = std::stoi(argv[2]);
    ChipId chip_id = std::stoi(argv[3]);
    const std::filesystem::path simulator_directory = argv[4];
    const std::filesystem::path cluster_descriptor_file = argv[5];
    auto cluster_desc = ClusterDescriptor::create_from_yaml(cluster_descriptor_file);
    ChildProcessTTSimChip child_process(chip_id, simulator_directory, cluster_desc.get(), read_fd, write_fd);
    return child_process.run();
}

}  // namespace tt::umd

// Global main function for the executable
int main(int argc, char* argv[]) { return tt::umd::child_process_main(argc, argv); }
