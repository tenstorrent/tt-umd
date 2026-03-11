/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

#include "umd/device/pcie/pci_device.hpp"

namespace tt::umd {

PcieProtocol::PcieProtocol(
    std::shared_ptr<PCIDevice> pci_device, architecture_implementation* architecture_impl, bool use_safe_api) :
    pci_device_(std::move(pci_device)),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(architecture_impl) {
    (void)use_safe_api;
}

void PcieProtocol::write_to_device(const void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("PcieProtocol::write_to_device not yet implemented");
}

void PcieProtocol::read_from_device(void*, tt_xy_pair, uint64_t, uint32_t) {
    throw std::runtime_error("PcieProtocol::read_from_device not yet implemented");
}

bool PcieProtocol::write_to_device_range(const void*, tt_xy_pair, tt_xy_pair, uint64_t, uint32_t) { return false; }

tt::ARCH PcieProtocol::get_arch() { return architecture_impl_->get_architecture(); }

architecture_implementation* PcieProtocol::get_architecture_implementation() { return architecture_impl_; }

int PcieProtocol::get_communication_device_id() const { return communication_device_id_; }

IODeviceType PcieProtocol::get_communication_device_type() { return IODeviceType::PCIe; }

void PcieProtocol::detect_hang_read(uint32_t) {
    throw std::runtime_error("PcieProtocol::detect_hang_read not yet implemented");
}

bool PcieProtocol::is_hardware_hung() {
    throw std::runtime_error("PcieProtocol::is_hardware_hung not yet implemented");
}

PCIDevice* PcieProtocol::get_pci_device() { return pci_device_.get(); }

void PcieProtocol::dma_write_to_device(const void*, size_t, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::dma_write_to_device not yet implemented");
}

void PcieProtocol::dma_read_from_device(void*, size_t, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::dma_read_from_device not yet implemented");
}

void PcieProtocol::dma_multicast_write(void*, size_t, tt_xy_pair, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::dma_multicast_write not yet implemented");
}

void PcieProtocol::dma_d2h(void*, uint32_t, size_t) {
    throw std::runtime_error("PcieProtocol::dma_d2h not yet implemented");
}

void PcieProtocol::dma_d2h_zero_copy(void*, uint32_t, size_t) {
    throw std::runtime_error("PcieProtocol::dma_d2h_zero_copy not yet implemented");
}

void PcieProtocol::dma_h2d(uint32_t, const void*, size_t) {
    throw std::runtime_error("PcieProtocol::dma_h2d not yet implemented");
}

void PcieProtocol::dma_h2d_zero_copy(uint32_t, const void*, size_t) {
    throw std::runtime_error("PcieProtocol::dma_h2d_zero_copy not yet implemented");
}

void PcieProtocol::noc_multicast_write(void*, size_t, tt_xy_pair, tt_xy_pair, uint64_t) {
    throw std::runtime_error("PcieProtocol::noc_multicast_write not yet implemented");
}

void PcieProtocol::write_regs(volatile uint32_t*, const uint32_t*, uint32_t) {
    throw std::runtime_error("PcieProtocol::write_regs not yet implemented");
}

void PcieProtocol::bar_write32(uint32_t, uint32_t) {
    throw std::runtime_error("PcieProtocol::bar_write32 not yet implemented");
}

uint32_t PcieProtocol::bar_read32(uint32_t) {
    throw std::runtime_error("PcieProtocol::bar_read32 not yet implemented");
}

}  // namespace tt::umd
