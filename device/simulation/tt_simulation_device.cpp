/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include <uv.h>
#include <nng/nng.h>

#include "common/logger.hpp"
#include "common/assert.hpp"
#include "device/driver_atomics.h"
#include "device/tt_cluster_descriptor.h"

#include "tt_simulation_device.h"
#include "tt_simulation_device_generated.h"

flatbuffers::FlatBufferBuilder create_flatbuffer(DEVICE_COMMAND rw, std::vector<uint32_t> vec, tt_cxy_pair core_, uint64_t addr, uint64_t size_=0){
    flatbuffers::FlatBufferBuilder builder;
    auto data = builder.CreateVector(vec);
    auto core = tt_vcs_core(core_.x, core_.y);
    uint64_t size = size_ == 0 ? size = vec.size()*sizeof(uint32_t) : size = size_;
    auto device_cmd = CreateDeviceRequestResponse(builder, rw, data, &core, addr, size);
    builder.Finish(device_cmd);
    return builder;
}

void print_flatbuffer(const DeviceRequestResponse *buf){    
    std::vector<uint32_t> data_vec(buf->data()->begin(), buf->data()->end());
    uint64_t addr = buf->address();
    uint32_t size = buf->size();
    tt_cxy_pair core = {0, buf->core()->x(), buf->core()->y()};
    
    std::stringstream ss;
    ss << std::hex << reinterpret_cast<uintptr_t>(addr);
    std::string addr_hex = ss.str();
    log_info(tt::LogEmulationDriver, "{} bytes @ address {} in core ({}, {})", size, addr_hex, core.x, core.y);
    for(int i = 0; i < data_vec.size(); i++){
        std::ios_base::fmtflags save = std::cout.flags();
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << data_vec[i] << " ";
        std::cout.flags(save);
    }
    std::cout << std::endl;
}

tt_SimulationDevice::tt_SimulationDevice(const std::string &sdesc_path) : tt_device(){
    log_info(tt::LogEmulationDriver, "Instantiating simulation device");
    soc_descriptor_per_chip.emplace(0, tt_SocDescriptor(sdesc_path));
    std::set<chip_id_t> target_devices = {0};
    
    // Start VCS simulator in a separate process
    TT_ASSERT(std::getenv("TT_REMOTE_EXE"), "TT_REMOTE_EXE not set, please provide path to the VCS binary");
    uv_loop_t *loop = uv_default_loop();
    uv_process_t child_p;
    uv_process_options_t child_options = {0};

    child_options.file = std::getenv("TT_REMOTE_EXE");
    child_options.flags = UV_PROCESS_DETACHED;

    int rv = uv_spawn(loop, &child_p, &child_options);
    if (rv) {
        TT_THROW("Failed to spawn simulator process: ", uv_strerror(rv));
    } else {
        log_info(tt::LogEmulationDriver, "Simulator process spawned with PID: {}", child_p.pid);
    }

    uv_unref((uv_handle_t *) &child_p);
    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
}

tt_SimulationDevice::~tt_SimulationDevice() {
    close_device();
}

// Setup/Teardown Functions
std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_SimulationDevice::get_virtual_soc_descriptors() {
    return soc_descriptor_per_chip;
}

void tt_SimulationDevice::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
    l1_address_params = l1_address_params_;
}

void tt_SimulationDevice::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {
    dram_address_params = dram_address_params_;
}

void tt_SimulationDevice::set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_) {
    host_address_params = host_address_params_;
}

void tt_SimulationDevice::set_driver_eth_interface_params(const tt_driver_eth_interface_params& eth_interface_params_) {
    eth_interface_params = eth_interface_params_;
}

void tt_SimulationDevice::start_device(const tt_device_params &device_params) {
    void *buf_ptr = nullptr;

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
    auto wr_buffer = create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, std::vector<uint32_t>(1, 0), {0, 0, 0}, 0);
    uint8_t *wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();

    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr));
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::deassert_risc_reset() {
    log_info(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
    auto wr_buffer = create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, std::vector<uint32_t>(1, 0), {0, 0, 0}, 0);
    uint8_t *wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();

    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::deassert_risc_reset_at_core(tt_cxy_pair core) {
    log_info(tt::LogEmulationDriver, "Sending 'deassert_risc_reset_at_core'.. (Not implemented, defaulting to 'deassert_risc_reset' instead)");
    deassert_risc_reset();
}

void tt_SimulationDevice::assert_risc_reset_at_core(tt_cxy_pair core) {
    log_info(tt::LogEmulationDriver, "Sending 'assert_risc_reset_at_core'.. (Not implemented, defaulting to 'assert_risc_reset' instead)");
    assert_risc_reset();
}

void tt_SimulationDevice::close_device() {
    // disconnect from remote connection
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    auto builder = create_flatbuffer(DEVICE_COMMAND_EXIT, std::vector<uint32_t>(1, 0), {0, 0, 0}, 0);
    host.send_to_device(builder.GetBufferPointer(), builder.GetSize());
}

// Runtime Functions
void tt_SimulationDevice::write_to_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
    log_info(tt::LogEmulationDriver, "Device writing");
    std::vector<std::uint32_t> data((uint32_t*)mem_ptr, (uint32_t*)mem_ptr + size_in_bytes / sizeof(uint32_t));
    auto wr_buffer = create_flatbuffer(DEVICE_COMMAND_WRITE, data, core, addr);
    uint8_t *wr_buffer_ptr = wr_buffer.GetBufferPointer();
    size_t wr_buffer_size = wr_buffer.GetSize();
    
    print_flatbuffer(GetDeviceRequestResponse(wr_buffer_ptr)); // sanity print
    host.send_to_device(wr_buffer_ptr, wr_buffer_size);
}

void tt_SimulationDevice::read_from_device(void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    void *rd_resp;

    // Send read request
    auto rd_req_buf = create_flatbuffer(DEVICE_COMMAND_READ, {0}, core, addr, size);
    host.send_to_device(rd_req_buf.GetBufferPointer(), rd_req_buf.GetSize());

    // Get read response
    size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

    auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);
    if (addr != 0x40){
        log_info(tt::LogEmulationDriver, "Device reading vec");
        print_flatbuffer(rd_resp_buf); // 0x40 is host polling device, don't print since it'll spam
    }
    std::memcpy(mem_ptr, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
}

void tt_SimulationDevice::wait_for_non_mmio_flush() {}
void tt_SimulationDevice::wait_for_non_mmio_flush(const chip_id_t chip) {}
void tt_SimulationDevice::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {}
void tt_SimulationDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {}
void tt_SimulationDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {}

// Misc. Functions to Query/Set Device State
std::unordered_map<chip_id_t, uint32_t> tt_SimulationDevice::get_harvesting_masks_for_soc_descriptors() {
    return {{0, 0}};
}

std::vector<chip_id_t> tt_SimulationDevice::detect_available_device_ids() {
    return {0};
}

std::set<chip_id_t> tt_SimulationDevice::get_target_remote_device_ids() {
    return target_remote_chips;
}

std::map<int,int> tt_SimulationDevice::get_clocks() {
    return {{0, 0}};
}

void *tt_SimulationDevice::host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const {
    return nullptr;
}

std::uint64_t tt_SimulationDevice::get_pcie_base_addr_from_device(const chip_id_t chip_id) const {
    if(arch_name == tt::ARCH::WORMHOLE_B0) {
        return 0x800000000;
    }
    else if (arch_name == tt::ARCH::BLACKHOLE) {
        // Enable 4th ATU window.
        return 1ULL << 60;
    }
    else {
        return 0;
    }
}

std::uint32_t tt_SimulationDevice::get_num_dram_channels(std::uint32_t device_id) {
    return get_soc_descriptor(device_id).get_num_dram_channels();
}

std::uint64_t tt_SimulationDevice::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    return get_soc_descriptor(device_id).dram_bank_size; // Space per channel is identical for now
}

std::uint32_t tt_SimulationDevice::get_num_host_channels(std::uint32_t device_id) {
    return 1;
}

std::uint32_t tt_SimulationDevice::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {return 0;}
std::uint32_t tt_SimulationDevice::get_numa_node_for_pcie_device(std::uint32_t device_id) {return 0;}
