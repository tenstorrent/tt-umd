// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/simulation_tt_device.hpp"

#include "simulation/simulation_server_socket.hpp"
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

bool SimulationTTDevice::get_noc_translation_enabled() {
    // Simulation backends operate on logical/virtual coordinates end-to-end; NOC translation is never
    // applied.
    return false;
}

void SimulationTTDevice::noc_multicast_write(
    const void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
    multicast_write_via_unicast(src, size, core_start, core_end, addr);
}

void SimulationTTDevice::noc_multicast_write(const void* src, size_t size, uint64_t addr) {
    UMD_THROW(error::RuntimeError, "NOC multicast write is not supported for simulation devices.");
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
    void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) {
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
