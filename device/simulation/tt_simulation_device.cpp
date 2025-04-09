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
#include "umd/device/driver_atomics.h"

static bool is_big_endian() {
    uint16_t v = 1;
    return reinterpret_cast<uint8_t*>(&v)[0] == 0;
}

template <typename T>
static void mem_swap(T* a, T* b) {
    T temp = *a;
    *a = *b;
    *b = temp;
}

template <typename T>
static void rev_arr(T* ptr, size_t elements) {
    size_t bytes = elements * sizeof(T);
    if (bytes == 0) return;
    auto* uptr = reinterpret_cast<unsigned char *>(ptr);
    size_t d = bytes - 1;
    for (size_t i = 0; i < bytes / 2; i ++) {
        mem_swap(&uptr[d--], &uptr[i]);
    }
}

template <typename T>
static void fromto_little_endian(T* ptr) {
    if (is_big_endian()) {
        rev_arr(ptr, 1);
    }
}

template <typename T>
static T endian_scalar(void const* ptr) {
    assert(ptr);
    T val = *reinterpret_cast<T const*>(ptr);
    fromto_little_endian(&val);
    return val;
}

// returned pointer can be null
static uint8_t const* table_get_fieldp(uint8_t const* table, size_t field) {
    // the subtract here is NOT a mistake
    uint8_t const* vtab = table - endian_scalar<int32_t>(table);
    // size_t vtab_len = endian_scalar<uint16_t>(vtab);
    // size_t tab_len = endian_scalar<uint16_t>(vtab + 2);
    uint16_t off = endian_scalar<uint16_t>(vtab+4 + field*2);
    if (off == 0) {
        return nullptr;
    } else {
        return table + off;
    }
}

struct FBVec {
    size_t len;
    uint8_t const* data;
};

static FBVec get_vec(void const* vec) {
    auto* offp = reinterpret_cast<uint8_t const *>(vec);
    auto* vecp = offp + endian_scalar<uint32_t>(offp);

    uint32_t len = endian_scalar<uint32_t>(vecp);
    auto* data = vecp + 4;
    return FBVec { len, data };
}

template <typename T>
static std::vector<T> read_scalar_vec(void const* vec) {
    FBVec fv = get_vec(vec);
    std::vector<T> out {};
    for (size_t i = 0; i < fv.len; i ++) {
        out.push_back(endian_scalar<T>(fv.data + i*sizeof(T)));
    }
    return out;
}

class FBWriter {
    std::vector<uint8_t> _bytes {};

    void overwrite_bytes_at(size_t pos, uint8_t const* ptr, size_t len) {
        for (size_t i = 0; i < len; i ++) {
            _bytes[pos + i] = ptr[i];
        }
    }

public:
    std::vector<uint8_t>&& drop() {
        return std::move(_bytes);
    }

    void write_bytes(uint8_t const* ptr, size_t len) {
        for (size_t i = 0; i < len; i ++) {
            _bytes.push_back(ptr[i]);
        }
    }

    template <typename T>
    void write_scalar(T val) {
        fromto_little_endian(&val);
        write_bytes(reinterpret_cast<uint8_t const *>(&val), sizeof(T));
    }

    template <typename T>
    void write_scalar_vec(std::vector<T> const& vec) {
        _bytes.reserve(8 + vec.size() * sizeof(T));
        write_scalar<uint32_t>(4);
        write_scalar(static_cast<uint32_t>(vec.size()));
        for (size_t i = 0; i < vec.size(); i ++) {
            write_scalar(vec[i]);
        }
    }

    size_t current_pos() {
        return _bytes.size();
    }

    void write_vtab(size_t tab_pos, size_t tab_end_pos, uint16_t const* offsets_from_tab, size_t fields) {
        size_t vtab_pos = current_pos();
        size_t tab_len = tab_end_pos - tab_pos;
        size_t vtab_len = fields * 2 + 4;

        // the negate here is not a mistake!
        auto off = - static_cast<int32_t>(vtab_pos - tab_pos);
        fromto_little_endian(&off);
        overwrite_bytes_at(tab_pos, reinterpret_cast<uint8_t const *>(&off), 4);

        _bytes.reserve(vtab_len);
        write_scalar<uint16_t>(vtab_len);
        write_scalar<uint16_t>(tab_len);
        for (size_t i = 0; i < fields; i ++) {
            write_scalar<uint16_t>(offsets_from_tab[i]);
        }
    }
};

enum class DEVICE_COMMAND : uint8_t {
    WRITE = 0,
    READ = 1,
    ALL_TENSIX_RESET_DEASSERT = 2,
    ALL_TENSIX_RESET_ASSERT = 3,
    START = 4,
    EXIT = 5,
};

// table DeviceRequestResponse {
//    command : DEVICE_COMMAND;
//    data    : [uint32];
//    core    : tt_vcs_core;
//    address : uint64;
//    size    : uint32;
// }
namespace DeviceRequestResponseFields {
    constexpr size_t COMMAND = 0;
    constexpr size_t DATA    = 1;
    constexpr size_t CORE    = 2;
    constexpr size_t ADDRESS = 3;
    constexpr size_t SIZE    = 4;
};

static std::vector<uint8_t> DeviceRequestResponse_Create(
        DEVICE_COMMAND command, std::vector<uint32_t> const& data, tt_xy_pair core, uint64_t addr, uint32_t size = 0)
{
    FBWriter writer;
    writer.write_scalar<uint32_t>(4); // offset to root table

    auto tab_pos = writer.current_pos();
    writer.write_scalar<uint32_t>(0); // vtable location; will be overwritten later

    // ========
    auto command_tab_off = writer.current_pos() - tab_pos;
    writer.write_scalar<uint8_t>(static_cast<uint8_t>(command));

    auto data_tab_off = writer.current_pos() - tab_pos;
    writer.write_scalar_vec(data);

    auto core_tab_off = writer.current_pos() - tab_pos;
    writer.write_scalar<uint64_t>(core.x);
    writer.write_scalar<uint64_t>(core.y);

    auto addr_tab_off = writer.current_pos() - tab_pos;
    writer.write_scalar<uint64_t>(addr);

    auto size_tab_off = writer.current_pos() - tab_pos;
    writer.write_scalar<uint32_t>(size);
    // ========

    size_t tab_end_pos = writer.current_pos();

    uint16_t offsets[] = {
        static_cast<uint16_t>(command_tab_off),
        static_cast<uint16_t>(data_tab_off),
        static_cast<uint16_t>(core_tab_off),
        static_cast<uint16_t>(addr_tab_off),
        static_cast<uint16_t>(size_tab_off),
    };

    writer.write_vtab(tab_pos, tab_end_pos, offsets, sizeof(offsets) / sizeof(*offsets));

    return writer.drop();
}

struct DeviceRequestResponse {
    DeviceRequestResponse(void const* data_ptr):
        // first u32le in blob is offset to root table
        ptr(reinterpret_cast<uint8_t const*>(data_ptr) + endian_scalar<uint32_t>(data_ptr))
    {}

    std::optional<DEVICE_COMMAND> command() const {
        constexpr size_t field = DeviceRequestResponseFields::COMMAND;

        auto* p = table_get_fieldp(ptr, field);
        if (!p) return std::nullopt;
        return static_cast<DEVICE_COMMAND>(
                endian_scalar<uint8_t>(p));
    }

    std::optional<std::vector<uint32_t>> data() const {
        constexpr size_t field = DeviceRequestResponseFields::DATA;

        auto* p = table_get_fieldp(ptr, field);
        if (!p) return std::nullopt;
        return read_scalar_vec<uint32_t>(p);
    }

    std::optional<tt_xy_pair> core() const {
        constexpr size_t field = DeviceRequestResponseFields::CORE;

        uint8_t const* xp = table_get_fieldp(ptr, field);
        if (!xp) return std::nullopt;
        size_t x = endian_scalar<uint64_t>(xp);
        size_t y = endian_scalar<uint64_t>(xp + 8);
        return tt_xy_pair { x, y };
    }

    std::optional<uint64_t> address() const {
        constexpr size_t field = DeviceRequestResponseFields::ADDRESS;

        auto* p = table_get_fieldp(ptr, field);
        if (!p) return std::nullopt;
        return endian_scalar<uint64_t>(p);
    }

    std::optional<uint32_t> size() const {
        constexpr size_t field = DeviceRequestResponseFields::SIZE;

        auto* p = table_get_fieldp(ptr, field);
        if (!p) return std::nullopt;
        return endian_scalar<uint64_t>(p);
    }

    void print() const {
        auto c = *core();
        std::stringstream ss;
        ss << std::hex << reinterpret_cast<uintptr_t>(*address());
        std::string addr_hex = ss.str();

        std::stringstream data_ss;
        auto data_vec = *data();
        for (int i = 0; i < data_vec.size(); i++) {
            data_ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << data_vec[i] << " ";
        }
        std::string data_hex = data_ss.str();

        log_debug(tt::LogEmulationDriver, "{} bytes @ address {} in core ({}, {})", *size(), addr_hex, c.x, c.y);
        log_debug(tt::LogEmulationDriver, "Data: {}", data_hex);
    }

private:
    // pointer to root table!
    uint8_t const* ptr;
};

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
    auto buf = DeviceRequestResponse(buf_ptr);
    auto cmd = buf.command();
    TT_ASSERT(cmd == DEVICE_COMMAND::EXIT, "Did not receive expected command from remote.");
    nng_free(buf_ptr, buf_size);
}

void tt_SimulationDevice::assert_risc_reset() {
    log_info(tt::LogEmulationDriver, "Sending assert_risc_reset signal..");
    auto wr_buffer =
        DeviceRequestResponse_Create(DEVICE_COMMAND::ALL_TENSIX_RESET_ASSERT, std::vector<uint32_t>(1, 0), {0, 0}, 0);

    host.send_to_device(wr_buffer.data(), wr_buffer.size());
}

void tt_SimulationDevice::deassert_risc_reset() {
    log_info(tt::LogEmulationDriver, "Sending 'deassert_risc_reset' signal..");
    auto wr_buffer =
        DeviceRequestResponse_Create(DEVICE_COMMAND::ALL_TENSIX_RESET_DEASSERT, std::vector<uint32_t>(1, 0), {0, 0}, 0);

    host.send_to_device(wr_buffer.data(), wr_buffer.size());
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
    auto buf = DeviceRequestResponse_Create(DEVICE_COMMAND::EXIT, std::vector<uint32_t>(1, 0), {0, 0}, 0);
    host.send_to_device(buf.data(), buf.size());
}

// Runtime Functions
void tt_SimulationDevice::write_to_device(
    const void* mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
    log_info(
        tt::LogEmulationDriver,
        "Device writing {} bytes to addr {} in core ({}, {})",
        size_in_bytes,
        addr,
        core.x,
        core.y);
    std::vector<std::uint32_t> data((uint32_t*)mem_ptr, (uint32_t*)mem_ptr + size_in_bytes / sizeof(uint32_t));
    auto wr_buffer = DeviceRequestResponse_Create(DEVICE_COMMAND::WRITE, data, {core.x, core.y}, addr);

    DeviceRequestResponse(wr_buffer.data()).print();  // sanity print
    host.send_to_device(wr_buffer.data(), wr_buffer.size());
}

void tt_SimulationDevice::write_to_device(
    const void* mem_ptr,
    uint32_t size_in_bytes,
    chip_id_t chip,
    tt::umd::CoreCoord core,
    uint64_t addr,
    const std::string& tlb_to_use) {
    write_to_device(mem_ptr, size_in_bytes, {(size_t)chip, translate_to_api_coords(chip, core)}, addr, tlb_to_use);
}

void tt_SimulationDevice::read_from_device(
    void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) {
    void* rd_resp;

    // Send read request
    auto rd_req_buf = DeviceRequestResponse_Create(DEVICE_COMMAND::READ, {0}, {core.x,core.y}, addr, size);
    host.send_to_device(rd_req_buf.data(), rd_req_buf.size());

    // Get read response
    size_t rd_rsp_sz = host.recv_from_device(&rd_resp);

    auto rd_resp_buf = DeviceRequestResponse(rd_resp);

    // Debug level polling as Metal will constantly poll the device, spamming the logs
    log_debug(tt::LogEmulationDriver, "Device reading vec");
    rd_resp_buf.print();

    std::memcpy(mem_ptr, rd_resp_buf.data()->data(), rd_resp_buf.data()->size() * sizeof(uint32_t));
    nng_free(rd_resp, rd_rsp_sz);
}

void tt_SimulationDevice::read_from_device(
    void* mem_ptr,
    chip_id_t chip,
    tt::umd::CoreCoord core,
    uint64_t addr,
    uint32_t size,
    const std::string& fallback_tlb) {
    read_from_device(mem_ptr, {(size_t)chip, translate_to_api_coords(chip, core)}, addr, size, fallback_tlb);
}

void tt_SimulationDevice::wait_for_non_mmio_flush() {}

void tt_SimulationDevice::wait_for_non_mmio_flush(const chip_id_t chip) {}

void tt_SimulationDevice::l1_membar(
    const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt::umd::CoreCoord>& cores) {}

void tt_SimulationDevice::dram_membar(
    const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {}

void tt_SimulationDevice::dram_membar(
    const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt::umd::CoreCoord>& cores) {}

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

std::uint32_t tt_SimulationDevice::get_num_dram_channels(std::uint32_t device_id) {
    return get_soc_descriptor(device_id).get_num_dram_channels();
}

std::uint64_t tt_SimulationDevice::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    return get_soc_descriptor(device_id).dram_bank_size;  // Space per channel is identical for now
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
