/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_simulation_device.h"

#include <nng/nng.h>
#include <uv.h>

#include <iostream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "tt_simulation_device_generated.h"
#include "umd/device/driver_atomics.h"

namespace tt::umd {

static_assert(!std::is_abstract<tt_SimulationDevice>(), "tt_SimulationDevice must be non-abstract.");

flatbuffers::FlatBufferBuilder create_flatbuffer(
    DEVICE_COMMAND rw, std::vector<uint32_t> vec, tt_xy_pair core_, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    auto core = tt_vcs_core(core_.x, core_.y);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, &core, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

static void print_flatbuffer(const DeviceRequestResponse* buf) {
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

tt_SimulationDeviceInit::tt_SimulationDeviceInit(
    const std::filesystem::path& simulator_directory, const bool noc_translation_enabled) :
    simulator_directory(simulator_directory),
    soc_descriptor(
        simulator_directory / "soc_descriptor.yaml", ChipInfo{.noc_translation_enabled = noc_translation_enabled}) {}

tt_SimulationDevice::tt_SimulationDevice(const tt_SimulationDeviceInit& init) : Chip(init.get_soc_descriptor()) {
    log_info(tt::LogEmulationDriver, "Instantiating simulation device");
    lock_manager.initialize_mutex(MutexType::TT_SIMULATOR);
    soc_descriptor_per_chip.emplace(0, init.get_soc_descriptor());
    arch_name = init.get_arch_name();
    target_devices_in_cluster = {0};

    // Start VCS simulator in a separate process
    std::filesystem::path simulator_path = init.get_simulator_path();
    if (!std::filesystem::exists(simulator_path)) {
        TT_THROW("Simulator binary not found at: ", simulator_path);
    }
    uv_loop_t* loop = uv_default_loop();
    std::string simulator_path_string = simulator_path;

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

    uv_unref((uv_handle_t*)&child_p);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
}

tt_SimulationDevice::~tt_SimulationDevice() { lock_manager.clear_mutex(MutexType::TT_SIMULATOR); }

void tt_SimulationDevice::start_device() {
    auto lock = lock_manager.acquire_mutex(MutexType::TT_SIMULATOR);
    void* buf_ptr = nullptr;

    host.start_host();

    log_info(tt::LogEmulationDriver, "Waiting for ack msg from remote...");
    size_t buf_size = host.recv_from_device(&buf_ptr);
    auto buf = GetDeviceRequestResponse(buf_ptr);
    auto cmd = buf->command();
    TT_ASSERT(cmd == DEVICE_COMMAND_EXIT, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);
}

void tt_SimulationDevice::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    auto lock = lock_manager.acquire_mutex(MutexType::TT_SIMULATOR);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
        auto wr_buffer =
            create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, std::vector<uint32_t>(1, 0), translate_core, 0);
        uint8_t* wr_buffer_ptr = wr_buffer.GetBufferPointer();
        size_t wr_buffer_size = wr_buffer.GetSize();

        print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));
        host.send_to_device(wr_buffer_ptr, wr_buffer_size);
    } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
        auto wr_buffer =
            create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, std::vector<uint32_t>(1, 0), translate_core, 0);
        uint8_t* wr_buffer_ptr = wr_buffer.GetBufferPointer();
        size_t wr_buffer_size = wr_buffer.GetSize();

        host.send_to_device(wr_buffer_ptr, wr_buffer_size);
    } else {
        TT_THROW("Invalid soft reset option.");
    }
}

void tt_SimulationDevice::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset(soc_descriptor_.get_coord_at({0, 0}, CoordSystem::TRANSLATED), soft_resets);
}

void tt_SimulationDevice::close_device() {
    // disconnect from remote connection
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    auto builder = create_flatbuffer(DEVICE_COMMAND_EXIT, std::vector<uint32_t>(1, 0), {0, 0}, 0);
    host.send_to_device(builder.GetBufferPointer(), builder.GetSize());
}

void tt_SimulationDevice::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void tt_SimulationDevice::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {}

// Runtime Functions
void tt_SimulationDevice::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    auto lock = lock_manager.acquire_mutex(MutexType::TT_SIMULATOR);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    std::vector<std::uint32_t> data((uint32_t*)src, (uint32_t*)src + size / sizeof(uint32_t));
    auto wr_buffer = create_flatbuffer(DEVICE_COMMAND_WRITE, data, translate_core, l1_dest);
    uint8_t* wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();

    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));  // sanity print
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    auto lock = lock_manager.acquire_mutex(MutexType::TT_SIMULATOR);
    void* rd_resp;

    // Send read request
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    auto rd_req_buf = create_flatbuffer(DEVICE_COMMAND_READ, {0}, translate_core, l1_src, size);
    host.send_to_device(rd_req_buf.GetBufferPointer(), rd_req_buf.GetSize());

    // Get read response
    size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

    auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);

    // Debug level polling as Metal will constantly poll the device, spamming the logs
    log_debug(tt::LogEmulationDriver, "Device reading vec");
    print_flatbuffer(rd_resp_buf);

    std::memcpy(dest, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
}

void tt_SimulationDevice::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void tt_SimulationDevice::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void tt_SimulationDevice::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    write_to_device(core, src, addr, size);
}

void tt_SimulationDevice::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    read_from_device(core, dst, addr, size);
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> tt_SimulationDevice::get_fast_pcie_static_tlb_write_callable() {
    throw std::runtime_error(
        "tt_SimulationDevice::get_fast_pcie_static_tlb_write_callable is not available for this chip.");
}

void tt_SimulationDevice::wait_for_non_mmio_flush() {}

void tt_SimulationDevice::l1_membar(const std::unordered_set<CoreCoord>& cores) {}

void tt_SimulationDevice::dram_membar(const std::unordered_set<uint32_t>& channels) {}

void tt_SimulationDevice::dram_membar(const std::unordered_set<CoreCoord>& cores) {}

void tt_SimulationDevice::deassert_risc_resets() {}

void tt_SimulationDevice::set_power_state(tt_DevicePowerState state) {}

int tt_SimulationDevice::get_clock() { return 0; }

int tt_SimulationDevice::arc_msg(
    uint32_t msg_code,
    bool wait_for_done,
    uint32_t arg0,
    uint32_t arg1,
    uint32_t timeout_ms,
    uint32_t* return_3,
    uint32_t* return_4) {
    *return_3 = 1;
    return 0;
}

int tt_SimulationDevice::get_num_host_channels() { return 0; }

int tt_SimulationDevice::get_host_channel_size(std::uint32_t channel) {
    throw std::runtime_error("There are no host channels available.");
}

void tt_SimulationDevice::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    throw std::runtime_error("tt_SimulationDevice::write_to_sysmem is not available for this chip.");
}

void tt_SimulationDevice::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    throw std::runtime_error("tt_SimulationDevice::read_from_sysmem is not available for this chip.");
}

int tt_SimulationDevice::get_numa_node() {
    throw std::runtime_error("tt_SimulationDevice::get_numa_node is not available for this chip.");
}

TTDevice* tt_SimulationDevice::get_tt_device() {
    throw std::runtime_error("tt_SimulationDevice::get_tt_device is not available for this chip.");
}

SysmemManager* tt_SimulationDevice::get_sysmem_manager() {
    throw std::runtime_error("tt_SimulationDevice::get_sysmem_manager is not available for this chip.");
}

TLBManager* tt_SimulationDevice::get_tlb_manager() {
    throw std::runtime_error("tt_SimulationDevice::get_tlb_manager is not available for this chip.");
}

}  // namespace tt::umd
