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
#include <vector>

#include "assert.hpp"
#include "logger.hpp"
#include "tt_simulation_device_generated.h"
#include "umd/device/driver_atomics.h"

flatbuffers::FlatBufferBuilder create_flatbuffer(
    DEVICE_COMMAND rw, std::vector<uint32_t> vec, tt_cxy_pair core_, uint64_t addr, uint64_t size_ = 0) {
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    auto core = tt_vcs_core(core_.x, core_.y);
    uint64_t size = (size_ == 0 ? vec.size() * sizeof(uint32_t) : size_);
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, &core, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

void print_flatbuffer(const DeviceRequestResponse* buf) {
    std::vector<uint32_t> data_vec(buf->data()->begin(), buf->data()->end());
    uint64_t addr = buf->address();
    uint32_t size = buf->size();
    tt_cxy_pair core = {0, buf->core()->x(), buf->core()->y()};

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
}

tt_SimulationDeviceInit::tt_SimulationDeviceInit(const std::filesystem::path& simulator_directory) :
    simulator_directory(simulator_directory), soc_descriptor(simulator_directory / "soc_descriptor.yaml", false) {}

tt_SimulationDevice::tt_SimulationDevice(const tt_SimulationDeviceInit& init) : tt_device() {
    log_info(tt::LogEmulationDriver, "Instantiating simulation device");
    soc_descriptor_per_chip.emplace(0, init.get_soc_descriptor());
    arch_name = init.get_arch_name();
    target_devices_in_cluster = {0};

    // Start VCS simulator in a separate process
    std::filesystem::path simulator_path = init.get_simulator_path();
    if (!std::filesystem::exists(simulator_path)) {
        TT_THROW("Simulator binary not found at: ", simulator_path);
    }
    uv_loop_t* loop = uv_default_loop();
    uv_process_t child_p;
    uv_process_options_t child_options = {0};
    std::string simulator_path_string = simulator_path;

    child_options.file = simulator_path_string.c_str();
    child_options.flags = UV_PROCESS_DETACHED;

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

tt_SimulationDevice::~tt_SimulationDevice() { close_device(); }

void tt_SimulationDevice::set_barrier_address_params(const barrier_address_params& barrier_address_params_) {}

void tt_SimulationDevice::start_device(const tt_device_params& device_params) {
    void* buf_ptr = nullptr;

    host.start_host();

    log_info(tt::LogEmulationDriver, "Waiting for ack msg from remote...");
    size_t buf_size = host.recv_from_device(&buf_ptr);
    auto buf = GetDeviceRequestResponse(buf_ptr);
    auto cmd = buf->command();
    TT_ASSERT(cmd == DEVICE_COMMAND_EXIT, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);
}

void tt_SimulationDevice::assert_risc_reset() {
    log_info(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
    auto wr_buffer =
        create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, std::vector<uint32_t>(1, 0), {0, 0, 0}, 0);
    uint8_t* wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();

    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::deassert_risc_reset() {
    log_info(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
    auto wr_buffer =
        create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, std::vector<uint32_t>(1, 0), {0, 0, 0}, 0);
    uint8_t* wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();

    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::deassert_risc_reset_at_core(
    const chip_id_t chip, const tt::umd::CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    log_info(
        tt::LogEmulationDriver,
        "Sending 'deassert_risc_reset_at_core'.. (Not implemented, defaulting to 'deassert_risc_reset' instead)");
    deassert_risc_reset();
}

void tt_SimulationDevice::assert_risc_reset_at_core(
    const chip_id_t chip, const tt::umd::CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    log_info(
        tt::LogEmulationDriver,
        "Sending 'assert_risc_reset_at_core'.. (Not implemented, defaulting to 'assert_risc_reset' instead)");
    assert_risc_reset();
}

void tt_SimulationDevice::close_device() {
    // disconnect from remote connection
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    auto builder = create_flatbuffer(DEVICE_COMMAND_EXIT, std::vector<uint32_t>(1, 0), {0, 0, 0}, 0);
    host.send_to_device(builder.GetBufferPointer(), builder.GetSize());
}

// Runtime Functions
void tt_SimulationDevice::write_to_device(
    const void* mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr) {
    log_info(
        tt::LogEmulationDriver,
        "Device writing {} bytes to addr {} in core ({}, {})",
        size_in_bytes,
        addr,
        core.x,
        core.y);
    std::vector<std::uint32_t> data((uint32_t*)mem_ptr, (uint32_t*)mem_ptr + size_in_bytes / sizeof(uint32_t));
    auto wr_buffer = create_flatbuffer(DEVICE_COMMAND_WRITE, data, core, addr);
    uint8_t* wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();

    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));  // sanity print
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::write_to_device(
    const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) {
    write_to_device(mem_ptr, size_in_bytes, {(size_t)chip, translate_to_api_coords(chip, core)}, addr);
}

void tt_SimulationDevice::read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size) {
    void* rd_resp;

    // Send read request
    auto rd_req_buf = create_flatbuffer(DEVICE_COMMAND_READ, {0}, core, addr, size);
    host.send_to_device(rd_req_buf.GetBufferPointer(), rd_req_buf.GetSize());

    // Get read response
    size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

    auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);

    // Debug level polling as Metal will constantly poll the device, spamming the logs
    log_debug(tt::LogEmulationDriver, "Device reading vec");
    print_flatbuffer(rd_resp_buf);

    std::memcpy(mem_ptr, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
}

void tt_SimulationDevice::read_from_device(
    void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size) {
    read_from_device(mem_ptr, {(size_t)chip, translate_to_api_coords(chip, core)}, addr, size);
}

void tt_SimulationDevice::wait_for_non_mmio_flush() {}

void tt_SimulationDevice::wait_for_non_mmio_flush(const chip_id_t chip) {}

void tt_SimulationDevice::l1_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores) {}

void tt_SimulationDevice::dram_membar(const chip_id_t chip, const std::unordered_set<uint32_t>& channels) {}

void tt_SimulationDevice::dram_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores) {}

// Misc. Functions to Query/Set Device State
std::vector<chip_id_t> tt_SimulationDevice::detect_available_device_ids() { return {0}; }

std::set<chip_id_t> tt_SimulationDevice::get_target_device_ids() { return target_devices_in_cluster; }

std::set<chip_id_t> tt_SimulationDevice::get_target_mmio_device_ids() { return target_devices_in_cluster; }

std::set<chip_id_t> tt_SimulationDevice::get_target_remote_device_ids() { return target_remote_chips; }

std::map<int, int> tt_SimulationDevice::get_clocks() { return {{0, 0}}; }

void* tt_SimulationDevice::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
    return nullptr;
}

std::uint64_t tt_SimulationDevice::get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
    if (arch_name == tt::ARCH::WORMHOLE_B0) {
        return 0x800000000;
    } else if (arch_name == tt::ARCH::BLACKHOLE) {
        // Enable 4th ATU window.
        return 1ULL << 60;
    } else {
        return 0;
    }
}

std::uint32_t tt_SimulationDevice::get_num_host_channels(std::uint32_t device_id) { return 1; }

std::uint32_t tt_SimulationDevice::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) { return 0; }

std::uint32_t tt_SimulationDevice::get_numa_node_for_pcie_device(std::uint32_t device_id) { return 0; }

const tt_SocDescriptor& tt_SimulationDevice::get_soc_descriptor(chip_id_t chip_id) const {
    return soc_descriptor_per_chip.at(chip_id);
};

void tt_SimulationDevice::configure_active_ethernet_cores_for_mmio_device(
    chip_id_t mmio_chip, const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip) {}

// TODO: This is a temporary function while we're switching between the old and the new API.
// Eventually, this function should be so small it would be obvioud to remove.
tt_xy_pair tt_SimulationDevice::translate_to_api_coords(
    const chip_id_t chip, const tt::umd::CoreCoord core_coord) const {
    return get_soc_descriptor(chip).translate_coord_to(core_coord, CoordSystem::VIRTUAL);
}
