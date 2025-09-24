/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/simulation/simulation_device.hpp"

#include <dlfcn.h>
#include <nng/nng.h>
#include <uv.h>

#include <iostream>
#include <string>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "assert.hpp"
#include "simulation_device_generated.h"
#include "umd/device/driver_atomics.hpp"

#define DLSYM_FUNCTION(func_name)                                                    \
    pfn_##func_name = (decltype(pfn_##func_name))dlsym(libttsim_handle, #func_name); \
    if (!pfn_##func_name) {                                                          \
        TT_THROW("Failed to find '%s' symbol: ", #func_name, dlerror());             \
    }

namespace tt::umd {

static_assert(!std::is_abstract<SimulationDevice>(), "SimulationDevice must be non-abstract.");

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

std::string SimulationDevice::get_soc_descriptor_path_from_simulator_path(const std::filesystem::path& simulator_path) {
    return (simulator_path.extension() == ".so") ? (simulator_path.parent_path() / "soc_descriptor.yaml")
                                                 : (simulator_path / "soc_descriptor.yaml");
}

SimulationDevice::SimulationDevice(const std::filesystem::path& simulator_directory, SocDescriptor soc_descriptor) :
    Chip(soc_descriptor) {
    log_info(tt::LogEmulationDriver, "Instantiating simulation device");
    soc_descriptor_per_chip.emplace(0, soc_descriptor);
    arch_name = soc_descriptor.arch;
    target_devices_in_cluster = {0};

    std::filesystem::path simulator_path = simulator_directory;
    if (!std::filesystem::exists(simulator_path)) {
        TT_THROW("Simulator binary not found at: ", simulator_path);
    }

    if (simulator_path.extension() == ".so") {
        // dlopen the simulator library and dlsym the entry points
        libttsim_handle = dlopen(simulator_path.string().c_str(), RTLD_LAZY);
        if (!libttsim_handle) {
            TT_THROW("Failed to dlopen simulator library: ", dlerror());
        }
        DLSYM_FUNCTION(libttsim_init)
        DLSYM_FUNCTION(libttsim_exit)
        DLSYM_FUNCTION(libttsim_tile_rd_bytes)
        DLSYM_FUNCTION(libttsim_tile_wr_bytes)
        DLSYM_FUNCTION(libttsim_tensix_reset_deassert)
        DLSYM_FUNCTION(libttsim_tensix_reset_assert)
        DLSYM_FUNCTION(libttsim_clock)
    } else {
        host.init();

        // Start simulator process
        uv_loop_t* loop = uv_default_loop();
        std::string simulator_path_string = simulator_path / "run.sh";
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

        uv_unref((uv_handle_t*)&child_p);
        uv_run(loop, UV_RUN_DEFAULT);
        uv_loop_close(loop);
    }
}

SimulationDevice::~SimulationDevice() {
    if (libttsim_handle) {
        dlclose(libttsim_handle);
    }
}

void SimulationDevice::start_device() {
    std::lock_guard<std::mutex> lock(device_lock);
    if (libttsim_handle) {
        pfn_libttsim_init();
    } else {
        void* buf_ptr = nullptr;

        host.start_host();

        log_info(tt::LogEmulationDriver, "Waiting for ack msg from remote...");
        size_t buf_size = host.recv_from_device(&buf_ptr);
        auto buf = GetDeviceRequestResponse(buf_ptr);
        auto cmd = buf->command();
        TT_ASSERT(cmd == DEVICE_COMMAND_EXIT, "Did not receive expected command from remote.");
        nng_free(buf_ptr, buf_size);
    }
}

void SimulationDevice::send_tensix_risc_reset(tt_xy_pair core, const TensixSoftResetOptions& soft_resets) {
    std::lock_guard<std::mutex> lock(device_lock);
    if (soft_resets == TENSIX_ASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
        if (libttsim_handle) {
            pfn_libttsim_tensix_reset_assert(core.x, core.y);
        } else {
            send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, core));
        }
    } else if (soft_resets == TENSIX_DEASSERT_SOFT_RESET) {
        log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
        if (libttsim_handle) {
            pfn_libttsim_tensix_reset_deassert(core.x, core.y);
        } else {
            send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, core));
        }
    } else {
        TT_THROW("Invalid soft reset option.");
    }
}

void SimulationDevice::send_tensix_risc_reset(CoreCoord core, const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset(tt_xy_pair(soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED)), soft_resets);
}

void SimulationDevice::send_tensix_risc_reset(const TensixSoftResetOptions& soft_resets) {
    send_tensix_risc_reset({0, 0}, soft_resets);
}

void SimulationDevice::assert_risc_reset(CoreCoord core, const RiscType selected_riscs) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'assert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    // If the architecture is Quasar, a special case is needed to control the NEO Data Movement cores.
    // Note that the simulator currently only supports soft reset control for all DMs on Quasar, you can't
    // control them individually. This is just a current API limitation, it is possible to add the support
    // for finer grained control in the future if needed.
    if (arch_name == tt::ARCH::QUASAR && selected_riscs == RiscType::ALL_NEO_DMS) {
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_ASSERT, translate_core));
    } else {
        // In case of Wormhole and Blackhole, we don't check which cores are selected, we just assert all tensix cores.
        // So the functionality is if we called with RiscType::ALL_TENSIX or RiscType::ALL.
        // In case of Quasar, this won't assert the NEO Data Movement cores, but will assert the Tensix cores.
        // For simplicity, we don't check and try to list all the combinations of selected_riscs arguments, we just
        // always call this command as if reset for all was requested, unless NEO_DMS_RESET was specifically selected.
        if (libttsim_handle) {
            pfn_libttsim_tensix_reset_assert(translate_core.x, translate_core.y);
        } else {
            send_command_to_simulation_host(
                host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_ASSERT, translate_core));
        }
    }
}

void SimulationDevice::deassert_risc_reset(CoreCoord core, const RiscType selected_riscs, bool staggered_start) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal for risc_type {}", selected_riscs);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    // See the comment in assert_risc_reset for more details.
    if (arch_name == tt::ARCH::QUASAR && selected_riscs == RiscType::ALL_NEO_DMS) {
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_ALL_NEO_DMS_RESET_DEASSERT, translate_core));
    } else {
        // See the comment in assert_risc_reset for more details.
        if (libttsim_handle) {
            pfn_libttsim_tensix_reset_deassert(translate_core.x, translate_core.y);
        } else {
            send_command_to_simulation_host(
                host, create_flatbuffer(DEVICE_COMMAND_ALL_TENSIX_RESET_DEASSERT, translate_core));
        }
    }
}

void SimulationDevice::close_device() {
    // disconnect from remote connection
    log_info(tt::LogEmulationDriver, "Sending exit signal to remote...");
    if (libttsim_handle) {
        pfn_libttsim_exit();
    } else {
        send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_EXIT, {0, 0}));
    }
}

void SimulationDevice::set_remote_transfer_ethernet_cores(const std::unordered_set<CoreCoord>& cores) {}

void SimulationDevice::set_remote_transfer_ethernet_cores(const std::set<uint32_t>& channels) {}

// Runtime Functions
void SimulationDevice::write_to_device(CoreCoord core, const void* src, uint64_t l1_dest, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    log_debug(tt::LogEmulationDriver, "Device writing {} bytes to l1_dest {} in core {}", size, l1_dest, core.str());
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    if (libttsim_handle) {
        pfn_libttsim_tile_wr_bytes(translate_core.x, translate_core.y, l1_dest, src, size);
        pfn_libttsim_clock(10);
    } else {
        std::vector<std::uint32_t> data((uint32_t*)src, (uint32_t*)src + size / sizeof(uint32_t));
        send_command_to_simulation_host(host, create_flatbuffer(DEVICE_COMMAND_WRITE, data, translate_core, l1_dest));
    }
}

void SimulationDevice::read_from_device(CoreCoord core, void* dest, uint64_t l1_src, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock);
    tt_xy_pair translate_core = soc_descriptor_.translate_coord_to(core, CoordSystem::TRANSLATED);
    if (libttsim_handle) {
        pfn_libttsim_tile_rd_bytes(translate_core.x, translate_core.y, l1_src, dest, size);
        pfn_libttsim_clock(10);
    } else {
        void* rd_resp;

        // Send read request
        send_command_to_simulation_host(
            host, create_flatbuffer(DEVICE_COMMAND_READ, {0}, translate_core, l1_src, size));

        // Get read response
        size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

        auto rd_resp_buf = GetDeviceRequestResponse(rd_resp);

        // Debug level polling as Metal will constantly poll the device, spamming the logs
        log_debug(tt::LogEmulationDriver, "Device reading vec");
        print_flatbuffer(rd_resp_buf);

        std::memcpy(dest, rd_resp_buf->data()->data(), rd_resp_buf->data()->size() * sizeof(uint32_t));
        nng_free(rd_resp, rd_rsp_sz);
    }
}

void SimulationDevice::write_to_device_reg(CoreCoord core, const void* src, uint64_t reg_dest, uint32_t size) {
    write_to_device(core, src, reg_dest, size);
}

void SimulationDevice::read_from_device_reg(CoreCoord core, void* dest, uint64_t reg_src, uint32_t size) {
    read_from_device(core, dest, reg_src, size);
}

void SimulationDevice::dma_write_to_device(const void* src, size_t size, CoreCoord core, uint64_t addr) {
    write_to_device(core, src, addr, size);
}

void SimulationDevice::dma_read_from_device(void* dst, size_t size, CoreCoord core, uint64_t addr) {
    read_from_device(core, dst, addr, size);
}

std::function<void(uint32_t, uint32_t, const uint8_t*)> SimulationDevice::get_fast_pcie_static_tlb_write_callable() {
    throw std::runtime_error(
        "SimulationDevice::get_fast_pcie_static_tlb_write_callable is not available for this chip.");
}

void SimulationDevice::wait_for_non_mmio_flush() {}

void SimulationDevice::l1_membar(const std::unordered_set<CoreCoord>& cores) {}

void SimulationDevice::dram_membar(const std::unordered_set<uint32_t>& channels) {}

void SimulationDevice::dram_membar(const std::unordered_set<CoreCoord>& cores) {}

void SimulationDevice::deassert_risc_resets() {}

void SimulationDevice::set_power_state(DevicePowerState state) {}

int SimulationDevice::get_clock() { return 0; }

int SimulationDevice::arc_msg(
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

int SimulationDevice::get_num_host_channels() { return 0; }

int SimulationDevice::get_host_channel_size(std::uint32_t channel) {
    throw std::runtime_error("There are no host channels available.");
}

void SimulationDevice::write_to_sysmem(uint16_t channel, const void* src, uint64_t sysmem_dest, uint32_t size) {
    throw std::runtime_error("SimulationDevice::write_to_sysmem is not available for this chip.");
}

void SimulationDevice::read_from_sysmem(uint16_t channel, void* dest, uint64_t sysmem_src, uint32_t size) {
    throw std::runtime_error("SimulationDevice::read_from_sysmem is not available for this chip.");
}

int SimulationDevice::get_numa_node() {
    throw std::runtime_error("SimulationDevice::get_numa_node is not available for this chip.");
}

TTDevice* SimulationDevice::get_tt_device() {
    throw std::runtime_error("SimulationDevice::get_tt_device is not available for this chip.");
}

SysmemManager* SimulationDevice::get_sysmem_manager() {
    throw std::runtime_error("SimulationDevice::get_sysmem_manager is not available for this chip.");
}

TLBManager* SimulationDevice::get_tlb_manager() {
    throw std::runtime_error("SimulationDevice::get_tlb_manager is not available for this chip.");
}

}  // namespace tt::umd
