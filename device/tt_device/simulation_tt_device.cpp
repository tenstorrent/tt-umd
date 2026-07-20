// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/simulation_tt_device.hpp"

#include <tt-logger/tt-logger.hpp>

#include "noc_access.hpp"
#include "simulation/simulation_server_socket.hpp"
#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/chip_helpers/simulation_tlb_allocator.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/tlb.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

// The constructor and destructor are defined out-of-line so that socket_
// (unique_ptr<SimulationServerSocket>) is constructed/destroyed where the type is complete; the
// public header only forward-declares SimulationServerSocket.
SimulationTTDevice::SimulationTTDevice(
    const std::filesystem::path& simulator_directory, std::unique_ptr<SimulationSysmemManager> sysmem_manager) :
    simulator_directory_(simulator_directory), sysmem_manager_(std::move(sysmem_manager)) {}

SimulationTTDevice::~SimulationTTDevice() = default;

void SimulationTTDevice::adopt_socket(std::unique_ptr<SimulationServerSocket> socket) { socket_ = std::move(socket); }

void SimulationTTDevice::write_to_device(
    const void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    // Client-mode devices run no local backend (tlb_allocator_/communicator_ are never built), so
    // device I/O is unavailable. Fail loudly instead of dereferencing a null communicator_.
    UMD_ASSERT(
        tlb_allocator_ != nullptr, error::RuntimeError, "Client-mode simulation device I/O is not available yet.");
    if (is_device_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    if (handle_special_write(mem_ptr, translated_core, addr, size)) {
        return;
    }
    if (should_use_cached_tlb_window()) {
        cached_tlb_window_->write_block_reconfigure(mem_ptr, translated_core, addr, size, get_selected_noc_id());
    } else {
        tile_write_bytes(translated_core, addr, mem_ptr, size);
    }
}

void SimulationTTDevice::read_from_device(void* mem_ptr, CoreCoord core, uint64_t addr, size_t size, NocId noc_id) {
    // Client-mode devices run no local backend (tlb_allocator_/communicator_ are never built), so
    // device I/O is unavailable. Fail loudly instead of dereferencing a null communicator_.
    UMD_ASSERT(
        tlb_allocator_ != nullptr, error::RuntimeError, "Client-mode simulation device I/O is not available yet.");
    if (is_device_closed()) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock(device_lock);
    xy_pair translated_core = get_soc_descriptor().translate_chip_coord_to_translated(core);
    if (handle_special_read(mem_ptr, translated_core, addr, size)) {
        return;
    }
    if (should_use_cached_tlb_window()) {
        cached_tlb_window_->read_block_reconfigure(mem_ptr, translated_core, addr, size, get_selected_noc_id());
    } else {
        tile_read_bytes(translated_core, addr, mem_ptr, size);
    }
    after_read();
}

void SimulationTTDevice::init_tlb_allocator(uint64_t bar0_base) {
    tlb_allocator_ = std::make_shared<SimulationTlbAllocator>(bar0_base, architecture_impl_.get());
}

void SimulationTTDevice::setup_cached_tlb_window() {
    // Quasar has no real TLBs; the communicator handles all I/O underneath. The 4GB size for Quasar is
    // a dummy value -- it just needs to be large enough so that TlbWindow::validate doesn't reject any
    // valid access (size 0 would cause division by zero in the TLB handle configure).
    static constexpr size_t SIZE_2MB = 2 * 1024 * 1024;
    static constexpr size_t SIZE_16MB = 16 * 1024 * 1024;
    static constexpr size_t SIZE_4GB = 4ULL * 1024 * 1024 * 1024;
    switch (arch) {
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
                tt::arch_to_str(arch));
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

bool SimulationTTDevice::get_noc_translation_enabled() {
    // Simulation backends operate on logical/virtual coordinates end-to-end; NOC translation is never
    // applied.
    return false;
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

void SimulationTTDevice::dma_d2h(void* dst, uint32_t src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported for simulation devices.");
}

void SimulationTTDevice::dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported for simulation devices.");
}

void SimulationTTDevice::dma_h2d(uint32_t dst, const void* src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported for simulation devices.");
}

void SimulationTTDevice::dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) {
    UMD_THROW(error::RuntimeError, "DMA operations are not supported for simulation devices.");
}

void SimulationTTDevice::dma_multicast_write(
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "DMA multicast write is not supported for simulation devices.");
}

void SimulationTTDevice::read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported for simulation devices.");
}

void SimulationTTDevice::write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported for simulation devices.");
}

void SimulationTTDevice::read_from_arc_csm(void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC CSM access is not supported for simulation devices.");
}

void SimulationTTDevice::write_to_arc_csm(const void* mem_ptr, uint64_t arc_addr_offset, [[maybe_unused]] size_t size) {
    UMD_THROW(error::RuntimeError, "ARC CSM access is not supported for simulation devices.");
}

uint32_t SimulationTTDevice::get_clock() {
    UMD_THROW(error::RuntimeError, "Getting clock is not supported for simulation devices.");
}

uint32_t SimulationTTDevice::get_min_clock_freq() {
    UMD_THROW(error::RuntimeError, "Getting minimum clock frequency is not supported for simulation devices.");
}

void SimulationTTDevice::retrain_dram_core(const uint32_t dram_channel) {
    UMD_THROW(error::RuntimeError, "DRAM retraining is not supported for simulation devices.");
}

}  // namespace tt::umd
