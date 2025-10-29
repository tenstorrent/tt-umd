/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/child_process_tt_sim_chip.hpp"
#include "umd/device/simulation/message_data.hpp"

#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"

#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle_, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find '{}' symbol: {}", #func_name, dlerror()); \
        return false;                                                                 \
    }

namespace tt::umd {

ChildProcessTTSimChip::ChildProcessTTSimChip(ChipId chip_id, const std::filesystem::path& simulator_directory, ClusterDescriptor* cluster_desc,
                          int comm_fd)
    : chip_id_(chip_id), simulator_directory_(simulator_directory),
      cluster_desc_(cluster_desc), comm_fd_(comm_fd),
      device_started_(false), should_exit_(false), libttsim_handle_(nullptr),
      libttsim_pci_device_id_(0) {

    architecture_impl_ = architecture_implementation::create(cluster_desc_->get_arch(chip_id_));
}

ChildProcessTTSimChip::~ChildProcessTTSimChip() {
    should_exit_ = true;
    unload_simulator_library();
}

int ChildProcessTTSimChip::run() {
    if (!load_simulator_library()) {
        TT_THROW("Failed to load simulator library for chip {}", chip_id_);
        return -1;
    }

    log_info(tt::LogEmulationDriver, "Child process started for chip {}", chip_id_);

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
            pfn_libttsim_clock(10);
        }
    }

    log_info(tt::LogEmulationDriver, "Child process exiting for chip {}", chip_id_);
    return 0;
}

bool ChildProcessTTSimChip::load_simulator_library() {
    if (!std::filesystem::exists(simulator_directory_)) {
        TT_THROW("Simulator binary not found at: {}", simulator_directory_.string());
        return false;
    }

    // Create a unique copy of the .so file with chip_id appended
    const auto sim_chip_dir_template = std::filesystem::temp_directory_path() / "umd_XXXXXX";
    const std::filesystem::path sim_chip_dir = mkdtemp(sim_chip_dir_template.string().data());
    const std::string filename = simulator_directory_.stem().string();
    const std::string extension = simulator_directory_.extension().string();

    copied_simulator_directory_ = sim_chip_dir / (filename + "_chip" + std::to_string(chip_id_) + extension);

    // Copy the original .so file to the new location
    std::filesystem::copy_file(
        simulator_directory_, copied_simulator_directory_, std::filesystem::copy_options::overwrite_existing);

    // dlopen the copied simulator library and dlsym the entry points
    libttsim_handle_ = dlopen(copied_simulator_directory_.string().c_str(), RTLD_LAZY);
    if (!libttsim_handle_) {
        TT_THROW("Failed to dlopen simulator library: {}", dlerror());
        return false;
    }

    DLSYM_FUNCTION(libttsim_init)
    DLSYM_FUNCTION(libttsim_configure_eth_socket)
    DLSYM_FUNCTION(libttsim_connect_eth_sockets)
    DLSYM_FUNCTION(libttsim_exit)
    DLSYM_FUNCTION(libttsim_pci_config_rd32)
    DLSYM_FUNCTION(libttsim_tile_rd_bytes)
    DLSYM_FUNCTION(libttsim_tile_wr_bytes)
    DLSYM_FUNCTION(libttsim_clock)

    setup_ethernet_connections();
    return true;
}

void ChildProcessTTSimChip::unload_simulator_library() {
    if (libttsim_handle_) {
        dlclose(libttsim_handle_);
        libttsim_handle_ = nullptr;
    }

    // Clean up the copied .so file
    if (!copied_simulator_directory_.empty() && std::filesystem::exists(copied_simulator_directory_)) {
        std::filesystem::remove_all(copied_simulator_directory_.parent_path());
    }
}

void ChildProcessTTSimChip::setup_ethernet_connections() {
    constexpr std::string_view tmp_dir = "/tmp/umd/tt_sim_chip";
    std::filesystem::create_directories(tmp_dir);
    auto get_remote_address = [&tmp_dir](uint64_t unique_chip_id, EthernetChannel channel, uint64_t remote_chip_id, EthernetChannel remote_channel) -> std::tuple<std::string, bool> {
        // TODO: We need to uniquify the directory per test to avoid collisions
        // Currently this will only work for one test per host if there are connections with same identifiers.

        // Create a deterministic ordering: smaller chip_id first, then smaller channel
        bool is_server = (unique_chip_id < remote_chip_id) ||
                        (unique_chip_id == remote_chip_id && channel < remote_channel);

        if (is_server) {
            return {fmt::format("{}/{}_{}_{}_{}", tmp_dir, unique_chip_id, channel, remote_chip_id, remote_channel), true};
        } else {
            return {fmt::format("{}/{}_{}_{}_{}", tmp_dir, remote_chip_id, remote_channel, unique_chip_id, channel), false};
        }
    };
    if (cluster_desc_->get_ethernet_connections().find(chip_id_) != cluster_desc_->get_ethernet_connections().end()) {
        auto unique_chip_id = cluster_desc_->get_chip_unique_ids().at(chip_id_);
        for (const auto& [channel, remote_chip_channel] : cluster_desc_->get_ethernet_connections().at(chip_id_)) {
            auto remote_chip_id = cluster_desc_->get_chip_unique_ids().at(std::get<0>(remote_chip_channel));
            auto remote_channel = std::get<1>(remote_chip_channel);
            auto [remote_address, is_server] = get_remote_address(unique_chip_id, channel, remote_chip_id, remote_channel);
            pfn_libttsim_configure_eth_socket(channel, remote_address.c_str(), is_server);
        }
    }
    if (cluster_desc_->get_ethernet_connections_to_remote_devices().find(chip_id_) != cluster_desc_->get_ethernet_connections_to_remote_devices().end()) {
        auto unique_chip_id = cluster_desc_->get_chip_unique_ids().at(chip_id_);
        for (const auto& [channel, remote_chip_channel] : cluster_desc_->get_ethernet_connections_to_remote_devices().at(chip_id_)) {
            auto remote_chip_id = std::get<0>(remote_chip_channel);
            auto remote_channel = std::get<1>(remote_chip_channel);
            auto [remote_address, is_server] = get_remote_address(unique_chip_id, channel, remote_chip_id, remote_channel);
            pfn_libttsim_configure_eth_socket(channel, remote_address.c_str(), is_server);
        }
    }
}


bool ChildProcessTTSimChip::read_message(Message& msg, std::vector<uint8_t>& data_buffer) {
    // Check if data is available without blocking
    struct pollfd pfd;
    pfd.fd = comm_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int result = poll(&pfd, 1, 0);  // 0 = non-blocking
    if (result == 0) {
        // No data available, return false to indicate no message
        return false;
    } else if (result < 0) {
        TT_THROW("Failed to check for available data: {}", strerror(errno));
    }

    // Data is available, read message header
    ssize_t bytes_read = safe_read(comm_fd_, &msg, sizeof(Message));
    if (bytes_read == 0) {
        // Parent process closed pipe
        should_exit_ = true;
        return false;
    } else if (bytes_read < 0) {
        TT_THROW("Failed to read message header: {}", strerror(errno));
    }

    if (bytes_read != sizeof(Message)) {
        TT_THROW("Incomplete message header read: expected {}, got {}", sizeof(Message), bytes_read);
    }

    // Read message data if present
    if (msg.size > 0) {
        data_buffer.resize(msg.size);
        bytes_read = safe_read(comm_fd_, data_buffer.data(), msg.size);
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

        case MessageType::READ_FROM_DEVICE:
            {
                std::vector<uint8_t> read_data = handle_read_from_device(data_buffer.data(), msg.size);
                send_response(true, read_data.data(), read_data.size());
            }
            break;


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

        case MessageType::CONNECT_ETH_SOCKETS:
            {
                bool result = handle_connect_eth_sockets();
                send_response(true, &result, sizeof(bool));
            }
            break;

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

    ssize_t bytes_written = safe_write(comm_fd_, &response, sizeof(Message));
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
        bytes_written = safe_write(comm_fd_, data, data_size);
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
    pfn_libttsim_init(chip_id_);

    // Read the PCI ID (first 32 bits of PCI config space)
    uint32_t pci_id = pfn_libttsim_pci_config_rd32(0, 0);
    uint32_t vendor_id = pci_id & 0xFFFF;
    libttsim_pci_device_id_ = pci_id >> 16;
    log_info(tt::LogEmulationDriver, "PCI vendor_id=0x{:x} device_id=0x{:x}", vendor_id, libttsim_pci_device_id_);
    TT_ASSERT(vendor_id == 0x1E52, "Unexpected PCI vendor ID.");

    device_started_ = true;
}

void ChildProcessTTSimChip::handle_close_device() {
    pfn_libttsim_exit();
    device_started_ = false;
}

void ChildProcessTTSimChip::handle_write_to_device(const void* data, uint32_t data_size) {
    // Data structure: WriteMessageData struct followed by data

    // Validate data size
    if (data_size < sizeof(WriteMessageData)) {
        TT_THROW("Invalid data size for write message: {} < {}", data_size, sizeof(WriteMessageData));
    }

    const WriteMessageData* msg_data = static_cast<const WriteMessageData*>(data);

    // Validate total data size matches expected size
    uint32_t expected_size = sizeof(WriteMessageData) + msg_data->size;
    if (data_size != expected_size) {
        TT_THROW("Data size mismatch for write message: expected {}, got {}", expected_size, data_size);
    }

    const uint8_t* data_ptr = static_cast<const uint8_t*>(data) + sizeof(WriteMessageData);

    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", msg_data->size, msg_data->l1_dest, msg_data->translated_core.str());
    pfn_libttsim_tile_wr_bytes(msg_data->translated_core.x, msg_data->translated_core.y, msg_data->l1_dest, data_ptr, msg_data->size);
}

std::vector<uint8_t> ChildProcessTTSimChip::handle_read_from_device(const void* data, uint32_t data_size) {
    // Data structure: ReadMessageData struct

    // Validate data size
    if (data_size != sizeof(ReadMessageData)) {
        TT_THROW("Invalid data size for read message: expected {}, got {}", sizeof(ReadMessageData), data_size);
    }

    const ReadMessageData* msg_data = static_cast<const ReadMessageData*>(data);

    // Allocate buffer for read data
    std::vector<uint8_t> read_buffer(msg_data->size);

    pfn_libttsim_tile_rd_bytes(msg_data->translated_core.x, msg_data->translated_core.y, msg_data->l1_src, read_buffer.data(), msg_data->size);

    return read_buffer;
}


void ChildProcessTTSimChip::handle_send_tensix_risc_reset(const void* data, uint32_t data_size) {
    // Data structure: TensixResetMessageData struct

    // Validate data size
    if (data_size != sizeof(TensixResetMessageData)) {
        TT_THROW("Invalid data size for tensix reset message: expected {}, got {}", sizeof(TensixResetMessageData), data_size);
    }

    const TensixResetMessageData* msg_data = static_cast<const TensixResetMessageData*>(data);

    if ((libttsim_pci_device_id_ == 0x401E) || (libttsim_pci_device_id_ == 0xB140)) {  // WH/BH
        uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
        uint32_t reset_value = uint32_t(msg_data->soft_resets);
        pfn_libttsim_tile_wr_bytes(
            msg_data->translated_core.x, msg_data->translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    } else {
        TT_THROW("Missing implementation of reset for this chip.");
    }
}

void ChildProcessTTSimChip::handle_assert_risc_reset(const void* data, uint32_t data_size) {
    // Data structure: AssertResetMessageData struct

    // Validate data size
    if (data_size != sizeof(AssertResetMessageData)) {
        TT_THROW("Invalid data size for assert reset message: expected {}, got {}", sizeof(AssertResetMessageData), data_size);
    }

    const AssertResetMessageData* msg_data = static_cast<const AssertResetMessageData*>(data);

    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", msg_data->selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(msg_data->selected_riscs);
    uint32_t reset_value;
    // core here is already translated
    pfn_libttsim_tile_rd_bytes(msg_data->translated_core.x, msg_data->translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    reset_value |= soft_reset_update;
    pfn_libttsim_tile_wr_bytes(msg_data->translated_core.x, msg_data->translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
}

void ChildProcessTTSimChip::handle_deassert_risc_reset(const void* data, uint32_t data_size) {
    // Data structure: DeassertResetMessageData struct

    // Validate data size
    if (data_size != sizeof(DeassertResetMessageData)) {
        TT_THROW("Invalid data size for deassert reset message: expected {}, got {}", sizeof(DeassertResetMessageData), data_size);
    }

    const DeassertResetMessageData* msg_data = static_cast<const DeassertResetMessageData*>(data);

    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", msg_data->selected_riscs);
    uint32_t soft_reset_addr = architecture_impl_->get_tensix_soft_reset_addr();
    uint32_t soft_reset_update = architecture_impl_->get_soft_reset_reg_value(msg_data->selected_riscs);
    uint32_t reset_value;
    pfn_libttsim_tile_rd_bytes(msg_data->translated_core.x, msg_data->translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
    reset_value &= ~soft_reset_update;
    pfn_libttsim_tile_wr_bytes(msg_data->translated_core.x, msg_data->translated_core.y, soft_reset_addr, &reset_value, sizeof(reset_value));
}

bool ChildProcessTTSimChip::handle_connect_eth_sockets() {
    return pfn_libttsim_connect_eth_sockets();
}

// Main function for child process
int child_process_main(int comm_fd, ChipId chip_id, const std::filesystem::path& simulator_directory,
                      ClusterDescriptor* cluster_desc) {
    ChildProcessTTSimChip child_process(chip_id, simulator_directory, cluster_desc, comm_fd);
    return child_process.run();
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        TT_THROW("Usage: {} <comm_fd> <chip_id> <simulator_directory> <cluster_descriptor_file>", argv[0]);
        return 1;
    }
    int comm_fd = std::stoi(argv[1]);
    ChipId chip_id = std::stoi(argv[2]);
    const std::filesystem::path simulator_directory = argv[3];
    const std::filesystem::path cluster_descriptor_file = argv[4];
    auto cluster_desc = ClusterDescriptor::create_from_yaml(cluster_descriptor_file);
    return child_process_main(comm_fd, chip_id, simulator_directory, cluster_desc.get());
}

}  // namespace tt::umd

// Global main function for the executable
int main(int argc, char* argv[]) {
    return tt::umd::main(argc, argv);
}
