// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_sim_communicator.hpp"

#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/vector.h>
#include <fmt/format.h>
#include <nng/nng.h>
#include <uv.h>

#include <cstring>
#include <exception>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "simulation_device_generated.h"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error.hpp"
#include "umd/device/noc_access.hpp"

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

RtlSimCommunicator::RtlSimCommunicator(const std::filesystem::path &simulator_directory, tt::ARCH arch) :
    simulator_directory_(simulator_directory), arch_(arch) {
    if (!std::filesystem::exists(simulator_directory_)) {
        UMD_THROW(
            error::RuntimeError, fmt::format("Simulator directory not found at: {}", simulator_directory_.string()));
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
        UMD_THROW(error::RuntimeError, fmt::format("Simulator binary not found at: {}", simulator_path_string));
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
        UMD_THROW(error::RuntimeError, fmt::format("Failed to spawn simulator process: {}", uv_strerror(rv)));
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
    UMD_ASSERT(cmd == DEVICE_COMMAND_EXIT, error::RuntimeError, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);

    // Start notification handler thread.
    log_info(tt::LogEmulationDriver, "Starting notification handler thread.");
    notification_thread_running_.store(true);
    notification_thread_ = std::thread(&RtlSimCommunicator::notification_handler_thread, this);
}

void RtlSimCommunicator::shutdown() {
    // Stop notification thread before shutting down communication.
    if (notification_thread_running_.load()) {
        log_info(tt::LogEmulationDriver, "Stopping notification handler thread.");
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
    NocId noc_id = get_selected_noc_id();

    if (noc_id == NocId::SYSTEM_NOC && arch_ != tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "System NOC is only supported on Grendel (Quasar) architecture.");
    }
    if (noc_id == NocId::NOC1 && arch_ == tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "NOC1 is not supported on Grendel (Quasar) architecture.");
    }

    {
        std::lock_guard<std::mutex> lock(device_lock_);
        tt_xy_pair core = {x, y};

        // Send read request.
        // System NOC only available with aether-main-v2026.W10.1_smn_support tag and 2x3_SMU config
        DEVICE_COMMAND command = (noc_id == NocId::SYSTEM_NOC) ? DEVICE_COMMAND_SMN_READ : DEVICE_COMMAND_READ;
        send_command_to_simulation_host(host_, create_flatbuffer(command, {0}, core, addr, size));
    }

    // Get read response from the command queue (populated by notification thread).
    auto msg = wait_for_command_response();
    if (msg.data == nullptr || msg.size == 0) {
        UMD_THROW(
            error::RuntimeError, "Failed to receive response from device - notification thread may have stopped.");
    }

    auto rd_resp_buf = GetDeviceRequestResponse(msg.data);

    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from address {} in core ({}, {})", size, addr, x, y);

    uint32_t response_bytes = rd_resp_buf->data()->size() * sizeof(uint32_t);
    UMD_ASSERT(
        response_bytes >= size,
        error::RuntimeError,
        fmt::format("tile_read_bytes response size {} is smaller than requested size {}.", response_bytes, size));
    std::memcpy(data, rd_resp_buf->data()->data(), size);
    nng_free(msg.data, msg.size);
}

void RtlSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    NocId noc_id = get_selected_noc_id();

    if (noc_id == NocId::SYSTEM_NOC && arch_ != tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "System NOC is only supported on Grendel (Quasar) architecture.");
    }
    if (noc_id == NocId::NOC1 && arch_ == tt::ARCH::QUASAR) {
        UMD_THROW(error::RuntimeError, "NOC1 is not supported on Grendel (Quasar) architecture.");
    }

    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to address {} in core ({}, {})", size, addr, x, y);

    tt_xy_pair core = {x, y};
    const uint32_t num_elements = size / sizeof(uint32_t);
    const auto *data_ptr = static_cast<const uint32_t *>(data);
    std::vector<uint32_t> data_vec(data_ptr, data_ptr + num_elements);

    // Send write request.
    // System NOC only available with aether-main-v2026.W10.1_smn_support tag and 2x3_SMU config
    DEVICE_COMMAND command = (noc_id == NocId::SYSTEM_NOC) ? DEVICE_COMMAND_SMN_WRITE : DEVICE_COMMAND_WRITE;
    send_command_to_simulation_host(host_, create_flatbuffer(command, data_vec, core, addr));
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

void RtlSimCommunicator::all_neo_dms_uncore_reset_assert() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_uncore_reset_assert signal.");
    tt_xy_pair core = {0, 0};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_UNCORE_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_uncore_reset_deassert() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_uncore_reset_deassert signal.");
    tt_xy_pair core = {0, 0};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_UNCORE_RESET_DEASSERT, core));
}

void RtlSimCommunicator::neo_dm_uncore_reset_assert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending neo_dm_uncore_reset_assert signal to core ({}, {}).", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_UNCORE_RESET_ASSERT, core));
}

void RtlSimCommunicator::neo_dm_uncore_reset_deassert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending neo_dm_uncore_reset_deassert signal to core ({}, {}).", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_UNCORE_RESET_DEASSERT, core));
}

void RtlSimCommunicator::set_ram_callbacks(RamWriteCallback write_cb, RamReadCallback read_cb) {
    ram_write_callback_ = std::move(write_cb);
    ram_read_callback_ = std::move(read_cb);
}

void RtlSimCommunicator::notification_handler_thread() {
    log_info(tt::LogEmulationDriver, "Notification handler thread started.");

    while (notification_thread_running_.load()) {
        void *buf_ptr = nullptr;
        size_t buf_size = 0;

        try {
            buf_size = host_.recv_from_device(&buf_ptr, 5000);

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
    uint64_t address = buf->address();
    uint32_t size = buf->size();

    log_debug(tt::LogEmulationDriver, "[AXI_RAM_WRITE] @ 0x{:016x} size={}.", address, size);

    if (ram_write_callback_ && buf->data() && buf->data()->size() > 0) {
        uint32_t payload_bytes = buf->data()->size() * sizeof(uint32_t);
        UMD_ASSERT(
            payload_bytes >= size,
            error::RuntimeError,
            fmt::format("RAM write notification payload {} is smaller than reported size {}.", payload_bytes, size));
        ram_write_callback_(address, buf->data()->data(), size);
    } else if (!ram_write_callback_) {
        log_warning(tt::LogEmulationDriver, "[AXI_RAM_WRITE] No callback registered, dropping write.");
    }
}

void RtlSimCommunicator::handle_ram_read_notification(const void *notification) {
    auto buf = GetDeviceRequestResponse(notification);
    uint64_t address = buf->address();
    uint32_t size = buf->size();

    log_debug(tt::LogEmulationDriver, "[AXI_RAM_READ] @ 0x{:016x} size={}.", address, size);

    // Flatbuffer data field is [uint32], so round up byte size to uint32 count.
    std::vector<uint32_t> read_data((size + 3) / 4, 0);

    if (ram_read_callback_) {
        ram_read_callback_(address, read_data.data(), size);
    } else {
        log_warning(tt::LogEmulationDriver, "[AXI_RAM_READ] No callback registered, returning zeros.");
    }

    // Echo back the core from the request so the simulator can route the response.
    tt_xy_pair core = {buf->core()->x(), 0};
    std::lock_guard<std::mutex> lock(device_lock_);
    send_command_to_simulation_host(
        host_, create_flatbuffer(DEVICE_COMMAND_AXI_RAM_READ_NOTIFICATION, read_data, core, address, size));
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
