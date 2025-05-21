// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include "umd/device/tt_device/remote_tt_device.h"

namespace tt::umd {

RemoteTTDevice::RemoteTTDevice(LocalChip *local_chip, eth_coord_t target_chip) :
    TTDevice(),
    local_chip_(local_chip),
    target_chip_(target_chip),
    remote_communication_(std::make_unique<RemoteCommunication>(local_chip)) {
    if (local_chip_->get_tt_device()->get_arch() != tt::ARCH::WORMHOLE_B0) {
        throw std::runtime_error("Creating remote TTDevice is supported only for Wormhole.");
    }
}

void RemoteTTDevice::wait_arc_core_start(const tt_xy_pair arc_core, const uint32_t timeout_ms) {}

ChipInfo RemoteTTDevice::get_chip_info() {
    throw std::runtime_error("get_chip_info() not implemented for RemoteTTDevice.");
}

uint32_t RemoteTTDevice::get_clock() { throw std::runtime_error("get_clock() not implemented for RemoteTTDevice."); }

uint32_t RemoteTTDevice::get_max_clock_freq() {
    throw std::runtime_error("get_max_clock_freq() not implemented for RemoteTTDevice.");
}

uint32_t RemoteTTDevice::get_min_clock_freq() {
    throw std::runtime_error("get_min_clock_freq() not implemented for RemoteTTDevice.");
}

BoardType RemoteTTDevice::get_board_type() {
    throw std::runtime_error("get_board_type() not implemented for RemoteTTDevice.");
}

std::vector<DramTrainingStatus> RemoteTTDevice::get_dram_training_status() {
    throw std::runtime_error("get_dram_training_status() not implemented for RemoteTTDevice.");
}

void RemoteTTDevice::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->read_non_mmio(target_chip_, core, mem_ptr, addr, size);
}

void RemoteTTDevice::write_to_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    remote_communication_->write_to_non_mmio(target_chip_, core, mem_ptr, addr, size);
}

void RemoteTTDevice::dma_d2h(void *dst, uint32_t src, size_t size) {
    throw std::runtime_error("PCIE DMA transfers not supported for RemoteTTDevice.");
}

void RemoteTTDevice::dma_h2d(uint32_t dst, const void *src, size_t size) {
    throw std::runtime_error("PCIE DMA transfers not supported for RemoteTTDevice.");
}

void RemoteTTDevice::dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) {
    throw std::runtime_error("PCIE DMA transfers not supported for RemoteTTDevice.");
}

void RemoteTTDevice::dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) {
    throw std::runtime_error("PCIE DMA transfers not supported for RemoteTTDevice.");
}

bool RemoteTTDevice::get_noc_translation_enabled() {
    throw std::runtime_error("get_noc_translation_enabled() not implemented for RemoteTTDevice.");
}

void RemoteTTDevice::wait_eth_core_training(const tt_xy_pair eth_core, const uint32_t timeout_ms) {}

}  // namespace tt::umd
