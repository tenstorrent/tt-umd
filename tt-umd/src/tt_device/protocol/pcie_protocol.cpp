// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/pcie_protocol.hpp"

#include <utility>

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

PcieProtocol::PcieProtocol(
    std::unique_ptr<PCIDevice> pci_device, ArchitectureImplementation* architecture_impl, IoSafety io_safety) :
    pci_device_(std::move(pci_device)), architecture_impl_(architecture_impl), io_safety_(io_safety) {}

PcieProtocol::~PcieProtocol() = default;

int PcieProtocol::get_mmio_id() { return pci_device_->get_device_num(); }

int PcieProtocol::get_numa_node() const { return pci_device_->get_numa_node(); }

std::optional<uint32_t> PcieProtocol::get_lane_count() const { return pci_device_->get_current_link_width(); }

PCIDevice* PcieProtocol::get_pci_device() { return pci_device_.get(); }

void PcieProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::read_data is not implemented yet");
}

void PcieProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::write_data is not implemented yet");
}

void PcieProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::read_ctrl is not implemented yet");
}

void PcieProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::write_ctrl is not implemented yet");
}

bool PcieProtocol::write_to_core_range(
    const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::write_to_core_range is not implemented yet");
}

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::bar_write32 is not implemented yet");
}

uint32_t PcieProtocol::bar_read32(uint32_t addr) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::bar_read32 is not implemented yet");
}

void PcieProtocol::set_power_state(PowerState state) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::set_power_state is not implemented yet");
}

int PcieProtocol::export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::export_dmabuf is not implemented yet");
}

void PcieProtocol::set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::set_io_timeout_callback is not implemented yet");
}

std::unique_ptr<TlbWindow> PcieProtocol::allocate_window(tlb_data config, TlbMapping mapping, size_t size) {
    UMD_THROW(error::RuntimeError, "PcieProtocol::allocate_window is not implemented yet");
}

TlbWindow& PcieProtocol::get_shared_window() {
    UMD_THROW(error::RuntimeError, "PcieProtocol::get_shared_window is not implemented yet");
}

}  // namespace tt::umd
