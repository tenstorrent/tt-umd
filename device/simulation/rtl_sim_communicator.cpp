// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_sim_communicator.hpp"

#include <nng/nng.h>
#include <uv.h>

#include <cstring>
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

RtlSimCommunicator::~RtlSimCommunicator() = default;

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
}

void RtlSimCommunicator::shutdown() {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_EXIT, {0, 0}));
}

void RtlSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    tt_xy_pair core = {x, y};
    void *rd_resp;

    // Send read request.
    send_command_to_simulation_host(host_, create_flatbuffer(DEVICE_COMMAND_READ, {0}, core, addr, size));

    // Get read response.
    size_t rd_rsp_sz = host_.recv_from_device(&rd_resp);
    auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);

    log_debug(tt::LogEmulationDriver, "Device reading {} bytes from address {} in core ({}, {})", size, addr, x, y);

    std::memcpy(data, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
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

}  // namespace tt::umd
