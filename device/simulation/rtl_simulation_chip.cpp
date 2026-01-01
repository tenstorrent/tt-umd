// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/rtl_simulation_chip.hpp"

#include <nng/nng.h>
#include <uv.h>

#include <iostream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "simulation_device_generated.h"

namespace tt::umd {

static_assert(!std::is_abstract<RtlSimulationChip>(), "RtlSimulationChip must be non-abstract.");

// Vector of DM RiscType values for iteration.
static const std::vector<RiscType> RISC_TYPES_DMS = {
    RiscType::DM0,
    RiscType::DM1,
    RiscType::DM2,
    RiscType::DM3,
    RiscType::DM4,
    RiscType::DM5,
    RiscType::DM6,
    RiscType::DM7};

static inline flatbuffers::FlatBufferBuilder create_flatbuffer(
    DEVICE_COMMAND rw, std::vector<uint32_t> vec, tt_xy_pair core_, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    auto core = tt_vcs_core(core_.x, core_.y);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, &core, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

static inline flatbuffers::FlatBufferBuilder create_flatbuffer(DEVICE_COMMAND rw, tt_xy_pair core) {
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

static inline void send_command_to_simulation_host(SimulationHost& host, flatbuffers::FlatBufferBuilder flat_buffer) {
    uint8_t* wr_buffer_ptr = flat_buffer.GetBufferPointer();
    size_t wr_buffer_size = flat_buffer.GetSize();
    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

RtlSimulationChip::RtlSimulationChip(
    const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor, ChipId chip_id) :
    SimulationChip(simulator_directory, soc_descriptor, chip_id) {
    log_info(tt::LogEmulationDriver, "Instantiating RTL simulation device");

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
        log_info(tt::LogEmulationDriver, "Simulator process spawned with PID: {}", child_p.pid);
    }

    uv_unref(reinterpret_cast<uv_handle_t*>(&child_p));
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
}

void RtlSimulationChip::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    void* buf_ptr = nullptr;

    host.start_host();

    log_info(tt::LogEmulationDriver, "Waiting for ack msg from remote...");
    size_t buf_size = host.recv_from_device(&buf_ptr);
    auto buf = GetDeviceRequestResponse(buf_ptr);
    auto cmd = buf->command();
    TT_ASSERT(cmd == DEVICE_COMMAND_EXIT, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);
}

void RtlSimulationChip::close_device() {
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_EXIT, {0, 0}));
}

void RtlSimulationChip::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    std::vector<std::uint32_t> data(
        static_cast<const uint32_t*>(src), static_cast<const uint32_t*>(src) + size / sizeof(uint32_t));
    send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_WRITE, data, translate_core, l1_dest));
}

void RtlSimulationChip::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    void* rd_resp;

    // Send read request.
    send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_READ, {0}, translate_core, l1_src, size));

    // Get read response.
    size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

    auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);

    // Debug level polling as Metal will constantly poll the device, spamming the logs.
    log_debug(tt::LogEmulationDriver, "Device reading vec");
    print_flatbuffer(rd_resp_buf);

    std::memcpy(dest, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
}

void RtlSimulationChip::send_tensix_risc_reset(tt_xy_pair translated_core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, translated_core));
    } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, translated_core));
    } else {
        TT_THROW("Invalid soft reset option.");
    }
}

void RtlSimulationChip::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset({0, 0}, soft_resets);
}

void RtlSimulationChip::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    if (arch_name == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            // Reset all DM cores.
            send_command_to_simulation_host(
                host, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_ASSERT, translate_core));
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                send_command_to_simulation_host(
                    host, create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_ASSERT, {0}, translate_core, i));
            }
        }
    }

    if (arch_name != tt::ARCH::QUASAR || (selected_riscs | RiscType::ALL_NEO_DMS) == RiscType::NONE) {
        // In case of Wormhole and Blackhole, we don't check which cores are selected, we just assert all tensix cores.
        // So the functionality is if we called with RiscType::ALL_TENSIX or RiscType::ALL.
        // In case of Quasar, this won't assert the NEO Data Movement cores, but will assert the Tensix cores.
        // For simplicity, we don't check and try to list all the combinations of selected_riscs arguments, we just
        // always call this command as if reset for all was requested.
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, translate_core));
    }
}

void RtlSimulationChip::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    // See the comment in assert_risc_reset for more details.
    if (arch_name == tt::ARCH::QUASAR) {
        if (selected_riscs == RiscType::ALL_NEO_DMS) {
            // Reset all DM cores.
            send_command_to_simulation_host(
                host, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_DEASSERT, translate_core));
            return;
        }
        // Check if this is a request per individual DM core reset.
        for (size_t i = 0; i < RISC_TYPES_DMS.size(); ++i) {
            if ((selected_riscs & RISC_TYPES_DMS[i]) != RiscType::NONE) {
                send_command_to_simulation_host(
                    host, create_flatbuffer(DEVICE_COMMAND_NEO_DM_RESET_DEASSERT, {0}, translate_core, i));
            }
        }
    }

    if (arch_name != tt::ARCH::QUASAR || (selected_riscs | RiscType::ALL_NEO_DMS) == RiscType::NONE) {
        // See the comment in assert_risc_reset for more details.
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, translate_core));
    }
}

}  // namespace tt::umd
