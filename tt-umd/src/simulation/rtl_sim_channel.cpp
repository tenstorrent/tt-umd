// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "simulation/rtl_sim_channel.hpp"

#include <flatbuffers/buffer.h>
#include <flatbuffers/vector.h>
#include <fmt/format.h>
#include <nng/nng.h>

#include <exception>
#include <tt-logger/tt-logger.hpp>
#include <utility>
#include <vector>

#include "simulation_device_generated.h"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

// As the communicator builds its messages: a size of 0 stands for the payload's size.
flatbuffers::FlatBufferBuilder create_message(
    DEVICE_COMMAND command, const std::vector<uint32_t> &data, uint32_t x, uint64_t addr, uint64_t size) {
    flatbuffers::FlatBufferBuilder builder;
    auto payload = builder.CreateVector(data);
    const tt_vcs_core core(x, 0);
    builder.Finish(CreateDeviceRequestResponse(
        builder, command, payload, &core, addr, size == 0 ? data.size() * sizeof(uint32_t) : size));
    return builder;
}

}  // namespace

RtlSimChannel::RtlSimChannel(SimulationHost &host) : host_(host) {}

RtlSimChannel::~RtlSimChannel() {
    stop_receiving();
    std::lock_guard<std::mutex> lock(responses_mutex_);
    while (!responses_.empty()) {
        const auto msg = responses_.front();
        responses_.pop();
        if (msg.data != nullptr && msg.size > 0) {
            nng_free(msg.data, msg.size);
        }
    }
}

void RtlSimChannel::attach() {
    std::lock_guard<std::mutex> lock(users_mutex_);
    if (users_++ == 0) {
        log_info(tt::LogEmulationDriver, "Starting notification handler thread.");
        receiving_.store(true);
        receive_thread_ = std::thread(&RtlSimChannel::receive_loop, this);
    }
}

void RtlSimChannel::detach(bool send_exit) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    UMD_ASSERT(users_ > 0, error::RuntimeError, "RtlSimChannel::detach() without a matching attach().");
    if (--users_ > 0) {
        return;
    }
    log_info(tt::LogEmulationDriver, "Stopping notification handler thread.");
    stop_receiving();
    if (send_exit) {
        log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
        send(create_message(DEVICE_COMMAND_EXIT, {0}, 0, 0, 0));
    }
}

void RtlSimChannel::stop_receiving() {
    if (receiving_.exchange(false)) {
        responses_cv_.notify_all();
    }
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

void RtlSimChannel::send(const flatbuffers::FlatBufferBuilder &message) {
    std::lock_guard<std::mutex> lock(send_mutex_);
    host_.send_to_device(message.GetBufferPointer(), message.GetSize());
}

RtlSimCommunicator::ReceivedMessage RtlSimChannel::wait_for_response() {
    std::unique_lock<std::mutex> lock(responses_mutex_);
    responses_cv_.wait(lock, [this] { return !responses_.empty() || !receiving_.load(); });
    if (responses_.empty()) {
        return {nullptr, 0};
    }
    const auto msg = responses_.front();
    responses_.pop();
    return msg;
}

void RtlSimChannel::set_ram_callbacks(
    RtlSimCommunicator::RamWriteCallback write_cb, RtlSimCommunicator::RamReadCallback read_cb) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (ram_write_callback_ || ram_read_callback_) {
        log_warning(
            tt::LogEmulationDriver,
            "Host memory callbacks are already set on this socket; its host memory requests keep going to the first "
            "device that set them.");
        return;
    }
    ram_write_callback_ = std::move(write_cb);
    ram_read_callback_ = std::move(read_cb);
}

void RtlSimChannel::receive_loop() {
    log_info(tt::LogEmulationDriver, "Notification handler thread started.");

    while (receiving_.load()) {
        void *buf_ptr = nullptr;
        size_t buf_size = 0;

        try {
            buf_size = host_.recv_from_device(&buf_ptr, 5000);
            if (buf_size == 0 || buf_ptr == nullptr) {
                continue;
            }

            const auto cmd = GetDeviceRequestResponse(buf_ptr)->command();
            if (cmd == DEVICE_COMMAND_AXI_RAM_WRITE_NOTIFICATION) {
                handle_ram_write(buf_ptr);
                nng_free(buf_ptr, buf_size);
            } else if (cmd == DEVICE_COMMAND_AXI_RAM_READ_NOTIFICATION) {
                handle_ram_read(buf_ptr);
                nng_free(buf_ptr, buf_size);
            } else {
                // A response to the request in flight: queue it for the communicator waiting on it.
                log_debug(
                    tt::LogEmulationDriver,
                    "Notification thread received regular command: {}, queueing.",
                    static_cast<int>(cmd));
                {
                    std::lock_guard<std::mutex> lock(responses_mutex_);
                    responses_.push({buf_ptr, buf_size});
                }
                responses_cv_.notify_one();
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

void RtlSimChannel::handle_ram_write(const void *notification) {
    const auto *buf = GetDeviceRequestResponse(notification);
    const uint64_t address = buf->address();
    const uint32_t size = buf->size();

    log_debug(tt::LogEmulationDriver, "[AXI_RAM_WRITE] @ 0x{:016x} size={}.", address, size);

    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    if (ram_write_callback_ && buf->data() && buf->data()->size() > 0) {
        const uint32_t payload_bytes = buf->data()->size() * sizeof(uint32_t);
        UMD_ASSERT(
            payload_bytes >= size,
            error::RuntimeError,
            fmt::format("RAM write notification payload {} is smaller than reported size {}.", payload_bytes, size));
        ram_write_callback_(address, buf->data()->data(), size);
    } else if (!ram_write_callback_) {
        log_warning(tt::LogEmulationDriver, "[AXI_RAM_WRITE] No callback registered, dropping write.");
    }
}

void RtlSimChannel::handle_ram_read(const void *notification) {
    const auto *buf = GetDeviceRequestResponse(notification);
    const uint64_t address = buf->address();
    const uint32_t size = buf->size();

    log_debug(tt::LogEmulationDriver, "[AXI_RAM_READ] @ 0x{:016x} size={}.", address, size);

    // Flatbuffer data field is [uint32], so round up byte size to uint32 count.
    std::vector<uint32_t> read_data((size + 3) / 4, 0);
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        if (ram_read_callback_) {
            ram_read_callback_(address, read_data.data(), size);
        } else {
            log_warning(tt::LogEmulationDriver, "[AXI_RAM_READ] No callback registered, returning zeros.");
        }
    }

    // Echo back the core from the request so the simulator can route the response.
    send(create_message(DEVICE_COMMAND_AXI_RAM_READ_NOTIFICATION, read_data, buf->core()->x(), address, size));
}

}  // namespace tt::umd
