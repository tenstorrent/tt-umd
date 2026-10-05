// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/simulation/rtl_sim_communicator.hpp"

#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/vector.h>
#include <fmt/format.h>
#include <nng/nng.h>

#include <cstring>
#include <exception>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "common/utils.hpp"
#include "simulation/word_access.hpp"
#include "simulation_device_generated.h"
#include "tt-umd/types/xy_pair.hpp"
#include "tt-umd/utils/error.hpp"

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
 * Create a flatbuffer for an access carrying no destination coordinate.
 *
 * The core field is left out rather than zeroed: an absent struct reads back as null, so a
 * simulator that tried to route on the coordinate would fault instead of targeting tile (0, 0).
 */
inline flatbuffers::FlatBufferBuilder create_global_flatbuffer(
    DEVICE_COMMAND rw, const std::vector<uint32_t> &vec, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, nullptr, addr, size);
    builder.Finish(device_cmd);
    return builder;
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
        UMD_THROW(
            error::RuntimeError, fmt::format("Simulator directory not found at: {}", simulator_directory_.string()));
    }
    owned_session_ = std::make_unique<RtlSimSession>(simulator_directory_, std::vector<std::string>{""});
}

RtlSimCommunicator::RtlSimCommunicator(SimulationHost &host) : host_(&host) {}

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

    if (owned_session_ != nullptr) {
        owned_session_->start();
        host_ = &owned_session_->get_host(0);
    }

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
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_EXIT, {0, 0}));
}

void RtlSimCommunicator::read_words(AccessKind kind, tt_xy_pair core, uint64_t addr, void *data, uint32_t size) {
    const char *name = kind == AccessKind::kGlobal ? "global" : (kind == AccessKind::kSmn ? "SMN" : "tile");
    {
        std::lock_guard<std::mutex> lock(device_lock_);
        if (kind == AccessKind::kGlobal) {
            send_command_to_simulation_host(
                *host_, create_global_flatbuffer(DEVICE_COMMAND_GLOBAL_READ, {0}, addr, size));
        } else {
            const DEVICE_COMMAND command = kind == AccessKind::kSmn ? DEVICE_COMMAND_SMN_READ : DEVICE_COMMAND_READ;
            send_command_to_simulation_host(*host_, create_flatbuffer(command, {0}, core, addr, size));
        }
    }

    // Get the response from the command queue (populated by the notification thread).
    auto msg = wait_for_command_response();
    if (msg.data == nullptr || msg.size == 0) {
        UMD_THROW(
            error::RuntimeError, "Failed to receive response from device - notification thread may have stopped.");
    }

    auto rd_resp_buf = GetDeviceRequestResponse(msg.data);
    if (rd_resp_buf->data() == nullptr || rd_resp_buf->data()->size() == 0) {
        // The remote answers a failed read with an EMPTY payload (its exception path); it is not a
        // response we can consume, and memcpy from a zero-length vector would fault on nullptr.
        const int resp_cmd = static_cast<int>(rd_resp_buf->command());
        const uint64_t resp_addr = rd_resp_buf->address();
        nng_free(msg.data, msg.size);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "{} read: the simulator returned an empty read response (command {}, address {:#x}) "
                "for a {} byte read at {:#x} - the simulator raised on this access; its own log has the details.",
                name,
                resp_cmd,
                resp_addr,
                size,
                addr));
    }

    const uint32_t response_bytes = rd_resp_buf->data()->size() * sizeof(uint32_t);
    if (response_bytes < size) {
        nng_free(msg.data, msg.size);
        UMD_THROW(
            error::RuntimeError,
            fmt::format(
                "{} read response size {} is smaller than requested size {} (address {:#x}).",
                name,
                response_bytes,
                size,
                addr));
    }
    std::memcpy(data, rd_resp_buf->data()->data(), size);
    nng_free(msg.data, msg.size);
}

void RtlSimCommunicator::write_words(AccessKind kind, tt_xy_pair core, uint64_t addr, const void *data, uint32_t size) {
    // Copied, since the caller's buffer need not be word-aligned.
    std::vector<uint32_t> words(size / sizeof(uint32_t));
    std::memcpy(words.data(), data, size);

    std::lock_guard<std::mutex> lock(device_lock_);
    if (kind == AccessKind::kGlobal) {
        send_command_to_simulation_host(*host_, create_global_flatbuffer(DEVICE_COMMAND_GLOBAL_WRITE, words, addr));
    } else {
        const DEVICE_COMMAND command = kind == AccessKind::kSmn ? DEVICE_COMMAND_SMN_WRITE : DEVICE_COMMAND_WRITE;
        send_command_to_simulation_host(*host_, create_flatbuffer(command, words, core, addr));
    }
}

void RtlSimCommunicator::read_bytes(AccessKind kind, tt_xy_pair core, uint64_t addr, void *data, uint32_t size) {
    // The wire format carries whole words and some remotes drop a partial tail word when they turn
    // the bytes they read into words, so read the whole words covering the range; see word_access.hpp.
    read_bytes_as_words(addr, data, size, [&](uint64_t a, void *d, uint32_t n) { read_words(kind, core, a, d, n); });
}

void RtlSimCommunicator::write_bytes(AccessKind kind, tt_xy_pair core, uint64_t addr, const void *data, uint32_t size) {
    // The wire format carries whole words and not every remote honours the byte count, so a partial
    // first or last word is read, merged and written back whole; see word_access.hpp.
    write_bytes_as_words(
        addr,
        data,
        size,
        [&](uint64_t a, void *d, uint32_t n) { read_words(kind, core, a, d, n); },
        [&](uint64_t a, const void *d, uint32_t n) { write_words(kind, core, a, d, n); });
}

void RtlSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(request_lock_);
    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from address {} in core ({}, {})", size, addr, x, y);
    read_bytes(AccessKind::kTile, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(request_lock_);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to address {} in core ({}, {})", size, addr, x, y);
    write_bytes(AccessKind::kTile, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::global_read_words(uint64_t addr, void *data, uint32_t size) {
    validate_register_access(addr, size);
    std::lock_guard<std::mutex> request_lock(request_lock_);
    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from global address {:#x}", size, addr);
    read_words(AccessKind::kGlobal, tt_xy_pair{0, 0}, addr, data, size);
}

void RtlSimCommunicator::global_write_words(uint64_t addr, const void *data, uint32_t size) {
    // The payload travels as words, so a partial word could only be dropped or written short.
    validate_register_access(addr, size);
    std::lock_guard<std::mutex> request_lock(request_lock_);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to global address {:#x}", size, addr);
    write_words(AccessKind::kGlobal, tt_xy_pair{0, 0}, addr, data, size);
}

void RtlSimCommunicator::smn_tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(request_lock_);
    log_debug(tt::LogEmulationDriver, "Device SMN reading {} bytes from address {} in core ({}, {})", size, addr, x, y);
    read_bytes(AccessKind::kSmn, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::smn_tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> request_lock(request_lock_);
    log_debug(tt::LogEmulationDriver, "Device SMN writing {} bytes to address {} in core ({}, {})", size, addr, x, y);
    write_bytes(AccessKind::kSmn, tt_xy_pair{x, y}, addr, data, size);
}

void RtlSimCommunicator::all_tensix_reset_assert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_tensix_reset_assert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_tensix_reset_deassert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_tensix_reset_deassert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_reset_assert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_reset_assert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_reset_deassert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_reset_deassert signal to core ({}, {})", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_DEASSERT, core));
}

void RtlSimCommunicator::neo_dm_reset_assert(uint32_t x, uint32_t y, uint32_t dm_index) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(
        tt::LogEmulationDriver, "Sending neo_dm_reset_assert signal to core ({}, {}) for DM index {}", x, y, dm_index);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_ASSERT, {0}, core, dm_index));
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
        *host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_DEASSERT, {0}, core, dm_index));
}

void RtlSimCommunicator::all_neo_dms_uncore_reset_assert() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_uncore_reset_assert signal.");
    tt_xy_pair core = {0, 0};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_UNCORE_RESET_ASSERT, core));
}

void RtlSimCommunicator::all_neo_dms_uncore_reset_deassert() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending all_neo_dms_uncore_reset_deassert signal.");
    tt_xy_pair core = {0, 0};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_UNCORE_RESET_DEASSERT, core));
}

void RtlSimCommunicator::neo_dm_uncore_reset_assert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending neo_dm_uncore_reset_assert signal to core ({}, {}).", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_UNCORE_RESET_ASSERT, core));
}

void RtlSimCommunicator::neo_dm_uncore_reset_deassert(uint32_t x, uint32_t y) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogEmulationDriver, "Sending neo_dm_uncore_reset_deassert signal to core ({}, {}).", x, y);
    tt_xy_pair core = {x, y};
    send_command_to_simulation_host(*host_, create_flatbuffer(DEVICE_COMMAND_NEO_DM_UNCORE_RESET_DEASSERT, core));
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
            buf_size = host_->recv_from_device(&buf_ptr, 5000);

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
        *host_, create_flatbuffer(DEVICE_COMMAND_AXI_RAM_READ_NOTIFICATION, read_data, core, address, size));
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
