// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"

#include <nng/nng.h>
#include <uv.h>

#include <tt-logger/tt-logger.hpp>

#include "assert.hpp"
#include "simulation_device_generated.h"

namespace tt::umd {

RtlSimulationTTDevice::RtlSimulationTTDevice(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor) :
    simulator_directory_(simulator_directory), soc_descriptor_(soc_descriptor) {
    log_info(LogUMD, "Instantiating RTL simulation device");

    if (!std::filesystem::exists(simulator_directory)) {
        TT_THROW("Simulator binary not found at: ", simulator_directory);
    }

    host.init();

    // Start simulator process.
    uv_loop_t* loop = uv_default_loop();
    std::string simulator_path_string = simulator_directory / "run.sh";
    if (!std::filesystem::exists(simulator_path_string)) {
        TT_THROW("Simulator binary not found at: ", simulator_path_string);
    }

    uv_stdio_container_t child_stdio[3];
    child_stdio[0].flags = UV_IGNORE;
    child_stdio[1].flags = UV_INHERIT_FD;
    child_stdio[1].data.fd = 1;
    child_stdio[2].flags = UV_INHERIT_FD;
    child_stdio[2].data.fd = 2;

    uv_process_options_t child_options = {0};
    child_options.file = simulator_path_string.c_str();
    child_options.flags = UV_PROCESS_DETACHED;
    child_options.stdio_count = 3;
    child_options.stdio = child_stdio;

    uv_process_t child_p;
    int rv = uv_spawn(loop, &child_p, &child_options);
    if (rv) {
        TT_THROW("Failed to spawn simulator process: ", uv_strerror(rv));
    } else {
        log_info(LogUMD, "Simulator process spawned with PID: {}", child_p.pid);
    }

    uv_unref(reinterpret_cast<uv_handle_t*>(&child_p));
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);

    start_host_communication();
}

inline flatbuffers::FlatBufferBuilder create_flatbuffer(
    DEVICE_COMMAND rw, std::vector<uint32_t> vec, tt_xy_pair core_, uint64_t addr, uint64_t size_ = 0) {
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

inline static void print_flatbuffer(const DeviceRequestResponse* buf) {
#ifdef DEBUG
    std::vector<uint32_t> data_vec(buf->data()->begin(), buf->data()->end());
    uint64_t addr = buf->address();
    uint32_t size = buf->size();
    tt_xy_pair core = {buf->core()->x(), buf->core()->y()};

    std::stringstream ss;
    ss << std::hex << reinterpret_cast<uintptr_t>(addr);
    std::string addr_hex = ss.str();

    std::stringstream data_ss;
    for (int i = 0; i < data_vec.size(); i++) {
        data_ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << data_vec[i] << " ";
    }
    std::string data_hex = data_ss.str();

    log_debug(tt::LogEmulationDriver, "{} bytes @ address {} in core ({}, {})", size, addr_hex, core.x, core.y);
    log_debug(tt::LogEmulationDriver, "Data: {}", data_hex);
#endif
}

inline void send_command_to_simulation_host(SimulationHost& host, flatbuffers::FlatBufferBuilder flat_buffer) {
    uint8_t* wr_buffer_ptr = flat_buffer.GetBufferPointer();
    size_t wr_buffer_size = flat_buffer.GetSize();
    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void RtlSimulationTTDevice::start_host_communication() {
    std::lock_guard<std::mutex> lock(device_lock);
    void* buf_ptr = nullptr;

    host.start_host();

    log_info(LogUMD, "Waiting for ack msg from remote...");
    size_t buf_size = host.recv_from_device(&buf_ptr);
    auto buf = GetDeviceRequestResponse(buf_ptr);
    auto cmd = buf->command();
    TT_ASSERT(cmd == DEVICE_COMMAND_EXIT, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);
}

void RtlSimulationTTDevice::write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(LogUMD, "Device writing {} bytes to l1_dest {} in core {}", size, addr, core.str());
    std::vector<std::uint32_t> data(
        static_cast<const uint32_t*>(mem_ptr), static_cast<const uint32_t*>(mem_ptr) + size / sizeof(uint32_t));
    send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_WRITE, data, core, addr));
}

void RtlSimulationTTDevice::read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    void* rd_resp;

    // Send read request.
    send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_READ, {0}, core, addr, size));

    // Get read response.
    size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

    auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);

    // Debug level polling as Metal will constantly poll the device, spamming the logs.
    log_debug(LogUMD, "Device reading vec");
    print_flatbuffer(rd_resp_buf);

    std::memcpy(mem_ptr, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
}

}  // namespace tt::umd
