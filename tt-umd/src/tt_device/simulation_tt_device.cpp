// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/simulation_tt_device.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <tt-logger/tt-logger.hpp>

#include "chip_helpers/simulation_tlb_allocator.hpp"
#include "noc_access.hpp"
#include "pcie/io_window_reconfigure.hpp"
#include "simulation/simulation_client.hpp"
#include "tt-umd/arch/architecture_implementation.hpp"
#include "tt-umd/pcie/tlb_window.hpp"
#include "tt-umd/simulation/simulation_server_protocol.hpp"
#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/tlb.hpp"
#include "tt-umd/utils/error.hpp"
#include "tt_device_model/simulation_tt_device_model.hpp"
#include "utils.hpp"

namespace tt::umd {

// The constructors and destructor are defined out-of-line so that client_ (unique_ptr<SimulationClient>) is
// constructed/destroyed where the type is complete; the public header only forward-declares SimulationClient.
SimulationTTDevice::ModelHandle::ModelHandle(std::unique_ptr<SimulationTTDeviceModel> simulation_model) :
    simulation_model(simulation_model.get()), model(std::move(simulation_model)) {}

SimulationTTDevice::SimulationTTDevice(
    ModelHandle model,
    const std::filesystem::path& simulator_directory,
    std::unique_ptr<SimulationSysmemManager> sysmem_manager) :
    TTDevice(std::move(model.model)),
    simulator_directory_(simulator_directory),
    sysmem_manager_(std::move(sysmem_manager)),
    simulation_model_(model.simulation_model) {}

SimulationTTDevice::SimulationTTDevice(
    ModelHandle model, const std::filesystem::path& simulator_directory, std::unique_ptr<SimulationClient> client) :
    TTDevice(std::move(model.model)),
    simulator_directory_(simulator_directory),
    client_(std::move(client)),
    simulation_model_(model.simulation_model) {}

SimulationTTDevice::~SimulationTTDevice() = default;

std::filesystem::path SimulationTTDevice::socket_path() const {
    return client_mode() ? client_->socket_path() : std::filesystem::path{};
}

void SimulationTTDevice::attach_client() { client_->attach(); }

void SimulationTTDevice::detach_client() {
    if (client_) {
        client_->detach();
    }
}

void SimulationTTDevice::write_to_device(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    // Client and host are disjoint paths with nothing shared; dispatch on the named role rather
    // than a bare client_ null-check. Each helper takes device_lock itself (the client path always;
    // the host path after its is_device_closed() gate), so the lock is never dropped on either.
    if (client_mode()) {
        client_write(core, addr, mem_ptr, size);
    } else {
        host_write(core, addr, mem_ptr, size);
    }
}

void SimulationTTDevice::read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    if (client_mode()) {
        client_read(core, addr, mem_ptr, size);
    } else {
        host_read(core, addr, mem_ptr, size);
    }
}

// One access per register, as in register_access.hpp; this device reaches registers directly instead of through
// a DeviceProtocol. Temporary: goes away when simulation moves under the DeviceProtocol umbrella.
void SimulationTTDevice::read_from_device_reg(
    void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    const size_t access_size = register_access_size(width);
    for (size_t offset = 0; offset < size; offset += access_size) {
        read_from_device(static_cast<uint8_t*>(mem_ptr) + offset, core, addr + offset, access_size, noc_id);
    }
}

void SimulationTTDevice::write_to_device_reg(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    validate_register_access_width(get_arch(), width);
    validate_register_access(addr, size, width);
    const size_t access_size = register_access_size(width);
    for (size_t offset = 0; offset < size; offset += access_size) {
        write_to_device(static_cast<const uint8_t*>(mem_ptr) + offset, core, addr + offset, access_size, noc_id);
    }
}

void SimulationTTDevice::noc_write_translated(tt_xy_pair core, uint64_t addr, const void* mem_ptr, size_t size) {
    if (is_device_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (handle_special_write(mem_ptr, core, addr, size)) {
        return;
    }
    if (global_address_mode_) {
        addr = att::resolve_translated(*noc_address_resolver_, get_soc_descriptor(), core, addr, size);
    }
    if (should_use_cached_tlb_window()) {
        write_block_reconfigure(*cached_tlb_window_, mem_ptr, core, addr, size, get_selected_noc_id());
    } else {
        tile_write_bytes(core, addr, mem_ptr, size);
    }
}

void SimulationTTDevice::noc_read_translated(tt_xy_pair core, uint64_t addr, void* mem_ptr, size_t size) {
    if (is_device_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    if (handle_special_read(mem_ptr, core, addr, size)) {
        return;
    }
    if (global_address_mode_) {
        addr = att::resolve_translated(*noc_address_resolver_, get_soc_descriptor(), core, addr, size);
    }
    if (should_use_cached_tlb_window()) {
        read_block_reconfigure(*cached_tlb_window_, mem_ptr, core, addr, size, get_selected_noc_id());
    } else {
        tile_read_bytes(core, addr, mem_ptr, size);
    }
    after_read();
}

void SimulationTTDevice::host_write(CoreCoord core, uint64_t addr, const void* mem_ptr, size_t size) {
    noc_write_translated(
        get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id()), addr, mem_ptr, size);
}

void SimulationTTDevice::host_read(CoreCoord core, uint64_t addr, void* mem_ptr, size_t size) {
    noc_read_translated(
        get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id()), addr, mem_ptr, size);
}

void SimulationTTDevice::client_write(CoreCoord core, uint64_t addr, const void* mem_ptr, size_t size) {
    // Serialized against the host's own access and other clients sharing the one socket.
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    // Translation is client-side and stateless (same SoC descriptor the host used); the host
    // receives an already-translated coordinate and passes it through verbatim.
    // The wire protocol carries size as uint32; reject a transfer that would truncate rather than
    // silently corrupt it. (Device transfers are far below this bound in practice.)
    UMD_ASSERT(
        size <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format("Remote write size {} exceeds the protocol maximum of {} bytes", size, UINT32_MAX));
    const xy_pair translated_core =
        get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    SimulationServerRequest request;
    request.command = SimulationServerCommand::WRITE;
    request.x = static_cast<uint32_t>(translated_core.x);
    request.y = static_cast<uint32_t>(translated_core.y);
    request.address = addr;
    request.size = static_cast<uint32_t>(size);
    const auto* bytes = static_cast<const uint8_t*>(mem_ptr);
    request.data.assign(bytes, bytes + size);

    const SimulationServerResponse response = decode_response(client_->transact(encode(request)));
    UMD_ASSERT(
        response.status == 0,
        error::RuntimeError,
        fmt::format("Remote simulation host failed the write (status {})", response.status));
}

void SimulationTTDevice::client_read(CoreCoord core, uint64_t addr, void* mem_ptr, size_t size) {
    // Serialized against the host's own access and other clients sharing the one socket.
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    // The wire protocol carries size as uint32; reject a read that would truncate it.
    UMD_ASSERT(
        size <= std::numeric_limits<uint32_t>::max(),
        error::RuntimeError,
        fmt::format("Remote read size {} exceeds the protocol maximum of {} bytes", size, UINT32_MAX));
    const xy_pair translated_core =
        get_soc_descriptor().translate_chip_coord_to_translated(core, get_selected_noc_id());
    SimulationServerRequest request;
    request.command = SimulationServerCommand::READ;
    request.x = static_cast<uint32_t>(translated_core.x);
    request.y = static_cast<uint32_t>(translated_core.y);
    request.address = addr;
    request.size = static_cast<uint32_t>(size);

    const SimulationServerResponse response = decode_response(client_->transact(encode(request)));
    UMD_ASSERT(
        response.status == 0,
        error::RuntimeError,
        fmt::format("Remote simulation host failed the read (status {})", response.status));
    UMD_ASSERT(
        response.data.size() == size,
        error::RuntimeError,
        fmt::format("Remote read returned {} bytes, expected {}", response.data.size(), size));
    if (size > 0) {
        std::memcpy(mem_ptr, response.data.data(), size);
    }
}

void SimulationTTDevice::init_tlb_allocator(uint64_t bar0_base, uint64_t bar4_base) {
    tlb_allocator_ = std::make_shared<SimulationTlbAllocator>(bar0_base, get_arch(), bar4_base);
}

void SimulationTTDevice::setup_cached_tlb_window() {
    // Quasar has no real TLBs; the communicator handles all I/O underneath. The 4GB size for Quasar is
    // a dummy value -- it just needs to be large enough so that TlbWindow::validate doesn't reject any
    // valid access (size 0 would cause division by zero in the TLB handle configure).
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    switch (get_arch()) {
        case tt::ARCH::BLACKHOLE:
            cached_tlb_window_ = get_io_window({}, TlbMapping::WC, SIZE_2MB);
            break;
        case tt::ARCH::WORMHOLE_B0:
            cached_tlb_window_ = get_io_window({}, TlbMapping::WC, SIZE_16MB);
            break;
        case tt::ARCH::QUASAR:
            cached_tlb_window_ = get_io_window({}, TlbMapping::WC, SIZE_4GB);
            break;
        default:
            log_debug(
                LogUMD,
                "Architecture {} does not support TLB allocation, leaving cached_tlb_window_ null.",
                tt::arch_to_str(get_arch()));
            break;
    }
}

std::unique_ptr<TlbWindow> SimulationTTDevice::get_io_window(tlb_data config, TlbMapping mapping, size_t size) {
    // tlb_allocator_ is only built by init_tlb_allocator(), which the client-mode path never calls
    // (it runs no local backend). Fail loudly instead of dereferencing a null allocator.
    UMD_ASSERT(
        tlb_allocator_ != nullptr,
        error::RuntimeError,
        "TLB allocator is not initialized; get_io_window is unavailable (e.g. in client mode).");
    int tlb_index = tlb_allocator_->allocate_tlb_index(size);
    if (tlb_index == -1) {
        UMD_THROW(error::RuntimeError, "No available TLB of requested size.");
    }
    // QUASAR bypasses the bitmap allocator (pools are empty by design); pass the requested size
    // through, since get_tlb_size_from_index has no pool to look up for the bypass index.
    size_t actual_size = (get_arch() == tt::ARCH::QUASAR) ? size : tlb_allocator_->get_tlb_size_from_index(tlb_index);
    return create_tlb_window(tlb_index, actual_size, mapping, config);
}

void SimulationTTDevice::noc_multicast_write(
    const void* src, size_t size, CoreCoord core_start, CoreCoord core_end, uint64_t addr, NocId noc_id) {
    multicast_write_via_unicast(src, size, core_start, core_end, addr, noc_id);
}

void SimulationTTDevice::noc_multicast_write(const void* src, size_t size, uint64_t addr, NocId noc_id) {
    auto [start, end] =
        get_soc_descriptor().get_bounding_rectangle(is_selected_noc1() ? CoordSystem::NOC1 : CoordSystem::NOC0);
    noc_multicast_write(src, size, start, end, addr, noc_id);
}

void SimulationTTDevice::dma_write_to_core_range(
    const void* src, uint64_t dst_addr, size_t size, CoreCoord core_start, CoreCoord core_end, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "DMA write to core range is not supported for simulation devices.");
}

}  // namespace tt::umd
