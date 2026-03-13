// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_sim_communicator.hpp"

#include <nng/nng.h>
#include <uv.h>

#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "simulation_device_generated.h"

namespace tt::umd {

namespace {

/**
 * Create a flatbuffer for device communication.
 */
inline flatbuffers::FlatBufferBuilder create_flatbuffer(
    DEVICE_COMMAND rw, const std::vector<uint32_t> &vec, tt_xy_pair core_, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    auto core = tt_vcs_core(core_.x, core_.y);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, &core, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

inline flatbuffers::FlatBufferBuilder create_flatbuffer(DEVICE_COMMAND rw, tt_xy_pair core) {
    return create_flatbuffer(rw, std::vector<uint32_t>(1, 0), core, 0);
}

/**
 * Send a command to the simulation host.
 */
inline void send_command_to_simulation_host(SimulationHost &host, const flatbuffers::FlatBufferBuilder &flat_buffer) {
    uint8_t *wr_buffer_ptr = flat_buffer.GetBufferPointer();
    size_t wr_buffer_size = flat_buffer.GetSize();
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

}  // namespace

RtlSimCommunicator::RtlSimCommunicator(const std::filesystem::path &simulator_directory) :
    simulator_directory_(simulator_directory) {
    if (!std::filesystem::exists(simulator_directory_)) {
        TT_THROW("Simulator directory not found at: {}", simulator_directory_.string());
    }
}

RtlSimCommunicator::~RtlSimCommunicator() {
    if (notification_thread_running_.load()) {
        notification_thread_running_.store(false);
        if (notification_thread_.joinable()) {
            notification_thread_.join();
        }
    }

    // Clean up any remaining messages in the command queue.
    std::lock_guard<std::mutex> lock(command_queue_mutex_);
    while (!command_queue_.empty()) {
        auto msg = command_queue_.front();
        command_queue_.pop();
        if (msg.data != nullptr && msg.size > 0) {
            nng_free(msg.data, msg.size);
        }
    }
}

void RtlSimCommunicator::initialize() {
    std::lock_guard<std::mutex> lock(device_lock_);

    log_info(tt::LogEmulationDriver, "Initializing RTL simulation communicator");

    host_.init();

    // Start simulator process.
    uv_loop_t *loop = uv_default_loop();
    std::string simulator_path_string = simulator_directory_ / "run.sh";
    if (!std::filesystem::exists(simulator_path_string)) {
        TT_THROW("Simulator binary not found at: {}", simulator_path_string);
    }

    uv_stdio_container_t child_stdio[3];
    child_stdio[0].flags = UV_IGNORE;
    child_stdio[1].flags = UV_INHERIT_FD;
    child_stdio[1].data.fd = 1;
    child_stdio[2].flags = UV_INHERIT_FD;
    child_stdio[2].data.fd = 2;

    uv_process_options_t child_options = {nullptr};
    child_options.file = simulator_path_string.c_str();
    child_options.flags = UV_PROCESS_DETACHED;
    child_options.stdio_count = 3;
    child_options.stdio = child_stdio;

    uv_process_t child_p;
    int rv = uv_spawn(loop, &child_p, &child_options);
    if (rv) {
        TT_THROW("Failed to spawn simulator process: {}", uv_strerror(rv));
    } else {
        log_info(tt::LogEmulationDriver, "Simulator process spawned with PID: {}", child_p.pid);
    }

    uv_unref(reinterpret_cast<uv_handle_t *>(&child_p));
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);

    // Start host and wait for acknowledgment.
    host_.start_host();

    log_info(tt::LogEmulationDriver, "Waiting for ack msg from remote...");
    void *buf_ptr = nullptr;
    size_t buf_size = host_.recv_from_device(&buf_ptr);
    auto buf = GetDeviceRequestResponse(buf_ptr);
    auto cmd = buf->command();
    TT_ASSERT(cmd == DEVICE_COMMAND_EXIT, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);

    // Start notification handler thread.
    log_info(tt::LogEmulationDriver, "Starting AXI RAM notification handler thread.");
    notification_thread_running_.store(true);
    notification_thread_ = std::thread(&RtlSimCommunicator::notification_handler_thread, this);
}

void RtlSimCommunicator::shutdown() {
    // Stop notification thread before shutting down communication.
    if (notification_thread_running_.load()) {
        log_info(tt::LogEmulationDriver, "Stopping AXI RAM notification handler thread.");
        notification_thread_running_.store(false);
        command_queue_cv_.notify_all();
        if (notification_thread_.joinable()) {
            notification_thread_.join();
        }
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_EXIT, {0, 0}));
}

void RtlSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    {
        std::lock_guard<std::mutex> lock(device_lock_);
        tt_xy_pair core = {x, y};

        // Send read request.
        send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_READ, {0}, core, addr, size));
    }

    // Get read response from the command queue (populated by notification thread).
    auto msg = wait_for_command_response();
    if (msg.data == nullptr || msg.size == 0) {
        TT_THROW("Failed to receive response from device - notification thread may have stopped.");
    }

    auto rd_resp_buf = GetDeviceRequestResponse(msg.data);

    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from address {} in core ({}, {})", size, addr, x, y);

    std::memcpy(data, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(msg.data, msg.size);
}

void RtlSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to address {} in core ({}, {})", size, addr, x, y);

    tt_xy_pair core = {x, y};
    const uint32_t num_elements = size / sizeof(uint32_t);
    const auto *data_ptr = static_cast<const uint32_t *>(data);
    std::vector<uint32_t> data_vec(data_ptr, data_ptr + num_elements);

    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_WRITE, data_vec, core, addr));
}

void RtlSimCommunicator::all_tensix_reset_assert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_tensix_reset_assert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_tensix_reset_deassert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_tensix_reset_deassert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_reset_assert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_reset_assert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_reset_deassert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_reset_deassert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_DEASSERT, core));
}

void RtlSimCommunicator::neo_dm_reset_assert(uint32_t x, uint32_t y, uint32_t dm_index) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(
        tt::LogEmulationDriver, "Sending neo_dm_reset_assert signal to core ({}, {}) for DM index {}", x, y, dm_index);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_ASSERT, {0}, core, dm_index));
}

void RtlSimCommunicator::neo_dm_reset_deassert(uint32_t x, uint32_t y, uint32_t dm_index) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(
        tt::LogEmulationDriver,
        "Sending neo_dm_reset_deassert signal to core ({}, {}) for DM index {}",
        x,
        y,
        dm_index);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(
        host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_DEASSERT, {0}, core, dm_index));
}

void RtlSimCommunicator::notification_handler_thread() {
    log_info(tt::LogEmulationDriver, "Notification handler thread started.");

    while (notification_thread_running_.load()) {
        void *buf_ptr = nullptr;
        size_t buf_size = 0;

        try {
            buf_size = host_.recv_from_device_with_timeout(&buf_ptr, 5000);

            if (buf_size == 0 || buf_ptr == nullptr) {
                continue;
            }

            auto buf = GetDeviceRequestResponse(buf_ptr);
            auto cmd = buf->command();

            if (cmd == DEVICE_COMMAND_AXI_RAM_WRITE_NOTIFICATION) {
                handle_ram_write_notification(buf_ptr);
                nng_free(buf_ptr, buf_size);
            } else if (cmd == DEVICE_COMMAND_AXI_RAM_READ_NOTIFICATION) {
                handle_ram_read_notification(buf_ptr);
                nng_free(buf_ptr, buf_size);
            } else {
                // Regular command response - queue it for the caller.
                log_debug(
                    tt::LogEmulationDriver,
                    "Notification thread received regular command: {}, queueing.",
                    static_cast<int>(cmd));

                ReceivedMessage msg;
                msg.data = buf_ptr;
                msg.size = buf_size;

                {
                    std::lock_guard<std::mutex> lock(command_queue_mutex_);
                    command_queue_.push(msg);
                }
                command_queue_cv_.notify_one();
            }
        } catch (const std::exception &e) {
            log_error(tt::LogEmulationDriver, "Error in notification handler thread: {}", e.what());
            if (buf_ptr != nullptr && buf_size > 0) {
                nng_free(buf_ptr, buf_size);
            }
        }
    }

    log_info(tt::LogEmulationDriver, "Notification handler thread stopped.");
}

void RtlSimCommunicator::handle_ram_write_notification(const void *notification) {
    auto buf = GetDeviceRequestResponse(notification);
    uint32_t ram_idx = buf->core()->x();
    uint64_t address = buf->address();
    uint32_t size = buf->size();

    log_info(tt::LogEmulationDriver, "[AXI_RAM_WRITE] RAM[{}] @ 0x{:08x} size={}.", ram_idx, address, size);

    if (buf->data() && buf->data()->size() > 0) {
        std::stringstream ss;
        size_t num_to_print = std::min<size_t>(4, buf->data()->size());
        for (size_t i = 0; i < num_to_print; i++) {
            ss << std::hex << std::setw(8) << std::setfill('0') << buf->data()->Get(i) << " ";
        }
        if (buf->data()->size() > 4) {
            ss << "...";
        }
        log_debug(tt::LogEmulationDriver, "  Data: {}", ss.str());
    }
}

void RtlSimCommunicator::handle_ram_read_notification(const void *notification) {
    auto buf = GetDeviceRequestResponse(notification);
    uint32_t ram_idx = buf->core()->x();
    uint64_t address = buf->address();
    uint32_t size = buf->size();

    log_info(
        tt::LogEmulationDriver,
        "[AXI_RAM_READ] RAM[{}] @ 0x{:08x} size={} - generating random data.",
        ram_idx,
        address,
        size);

    // Generate random data for the read response.
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    uint32_t num_uint32s = (size + 3) / 4;
    std::vector<uint32_t> random_data(num_uint32s);
    for (uint32_t i = 0; i < num_uint32s; i++) {
        random_data[i] = dist(gen);
    }

    if (!random_data.empty()) {
        std::stringstream ss;
        size_t num_to_print = std::min<size_t>(4, random_data.size());
        for (size_t i = 0; i < num_to_print; i++) {
            ss << std::hex << std::setw(8) << std::setfill('0') << random_data[i] << " ";
        }
        if (random_data.size() > 4) {
            ss << "...";
        }
        log_debug(tt::LogEmulationDriver, "  Sending data: {}", ss.str());
    }

    // Send response back to remote with random data.
    tt_xy_pair core = {ram_idx, 0};
    std::lock_guard<std::mutex> lock(device_lock_);
    send_command_to_simulation_host(
        host_, create_flatbuffer(DEVICE_COMMAND_AXI_RAM_READ_NOTIFICATION, random_data, core, address, size));

    log_debug(tt::LogEmulationDriver, "[AXI_RAM_READ] Response sent.");
}

RtlSimCommunicator::ReceivedMessage RtlSimCommunicator::wait_for_command_response() {
    std::unique_lock<std::mutex> lock(command_queue_mutex_);
    command_queue_cv_.wait(lock, [this] { return !command_queue_.empty() || !notification_thread_running_.load(); });

    if (!command_queue_.empty()) {
        auto msg = command_queue_.front();
        command_queue_.pop();
        return msg;
    }

    // Notification thread has stopped, return empty message.
    return {nullptr, 0};
}

}  // namespace tt::umd
