// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/simulation/tt_sim_communicator.hpp"

#include <tt-logger/tt-logger.hpp>
#include <utility>

#include "umd/device/simulation/sim_backend.hpp"
#include "umd/device/simulation/sim_backend_registry.hpp"
#include "umd/device/utils/error.hpp"

namespace tt::umd {

TTSimCommunicator::TTSimCommunicator(const std::filesystem::path &simulator_directory, bool copy_sim_binary) :
    simulator_directory_(simulator_directory), copy_sim_binary_(copy_sim_binary) {}

TTSimCommunicator::~TTSimCommunicator() = default;

void TTSimCommunicator::initialize() {
    std::lock_guard<std::mutex> lock(device_lock_);
    // Selection-by-probing happens here: the registry picks the backend whose required symbol
    // set resolves in this simulator library and constructs it.
    backend_ = create_sim_backend(simulator_directory_, copy_sim_binary_);
}

void TTSimCommunicator::start_sim() {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->init();
}

void TTSimCommunicator::shutdown() {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->shutdown();
}

void TTSimCommunicator::tile_write_bytes(uint32_t x, uint32_t y, uint64_t addr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    log_debug(tt::LogUMD, "Device writing {} bytes to l1_dest {} in core ({},{})", size, addr, x, y);
    backend_->tile_write(x, y, addr, data, size);
}

void TTSimCommunicator::tile_read_bytes(uint32_t x, uint32_t y, uint64_t addr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->tile_read(x, y, addr, data, size);
}

void TTSimCommunicator::pci_mem_read_bytes(uint64_t paddr, void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->pci_mem_read(paddr, data, size);
}

void TTSimCommunicator::pci_mem_write_bytes(uint64_t paddr, const void *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->pci_mem_write(paddr, data, size);
}

uint32_t TTSimCommunicator::pci_config_read32(uint32_t bus_device_function, uint32_t offset) {
    std::lock_guard<std::mutex> lock(device_lock_);
    return backend_->pci_config_read32(bus_device_function, offset);
}

void TTSimCommunicator::advance_clock(uint32_t n_clocks) {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->advance_clock(n_clocks);
}

void TTSimCommunicator::set_pcie_dma_mem_callbacks(
    std::function<void(uint64_t, void *, uint32_t)> pfn_pci_dma_mem_rd_bytes,
    std::function<void(uint64_t, const void *, uint32_t)> pfn_pci_dma_mem_wr_bytes) {
    std::lock_guard<std::mutex> lock(device_lock_);
    backend_->set_dma_callbacks(std::move(pfn_pci_dma_mem_rd_bytes), std::move(pfn_pci_dma_mem_wr_bytes));
}

bool TTSimCommunicator::supports(SimCapability cap) const {
    std::lock_guard<std::mutex> lock(device_lock_);
    return backend_ && backend_->supports(cap);
}

bool TTSimCommunicator::setup_multichip(uint32_t num_chips) {
    std::lock_guard<std::mutex> lock(device_lock_);
    return backend_->setup_multichip(num_chips);
}

}  // namespace tt::umd
