// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/protocol/pcie_protocol.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <utility>

#include "pcie/io_window_reconfigure.hpp"
#include "tt-umd/arch/architecture_tlbs.hpp"
#include "tt-umd/pcie/silicon_tlb_window.hpp"
#include "tt-umd/utils/error.hpp"

namespace tt::umd {

namespace {

std::unique_ptr<TlbWindow> make_window(PCIDevice& pci_device, TlbMapping mapping, IoSafety io_safety) {
    const size_t size = get_architecture_tlbs(pci_device.get_arch()).cached_window_size;
    return std::make_unique<SiliconTlbWindow>(pci_device.allocate_tlb(size, mapping), tlb_data{}, io_safety);
}

volatile uint32_t* bar_register(PCIDevice& pci_device, uint32_t addr) {
    if (pci_device.bar0 == nullptr) {
        UMD_THROW(error::RuntimeError, fmt::format("PCI device {} has no BAR0 mapping.", pci_device.get_device_num()));
    }
    if (addr % sizeof(uint32_t) != 0) {
        UMD_THROW(error::RuntimeError, fmt::format("BAR address {:#x} is not 4-byte aligned.", addr));
    }
    return reinterpret_cast<volatile uint32_t*>(reinterpret_cast<uintptr_t>(pci_device.bar0) + addr);
}

}  // namespace

PcieProtocol::PcieProtocol(std::unique_ptr<PCIDevice> pci_device, IoSafety io_safety) :
    pci_device_(std::move(pci_device)) {
    data_write_window_.window = make_window(*pci_device_, TlbMapping::WC, io_safety);
    data_read_window_.window = make_window(*pci_device_, TlbMapping::UC, io_safety);
    ctrl_window_.window = make_window(*pci_device_, TlbMapping::UC, io_safety);
}

void PcieProtocol::read_data(void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(data_read_window_.mutex);
    read_block_reconfigure(*data_read_window_.window, dst, core, addr, size, noc_id);
}

void PcieProtocol::write_data(const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(data_write_window_.mutex);
    write_block_reconfigure(*data_write_window_.window, src, core, addr, size, noc_id);
}

void PcieProtocol::read_ctrl(
    void* dst, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    std::lock_guard<std::mutex> lock(ctrl_window_.mutex);
    read_register_reconfigure(*ctrl_window_.window, dst, core, addr, size, noc_id, width);
}

void PcieProtocol::write_ctrl(
    const void* src, tt_xy_pair core, uint64_t addr, size_t size, NocId noc_id, RegisterAccessWidth width) {
    std::lock_guard<std::mutex> lock(ctrl_window_.mutex);
    write_register_reconfigure(*ctrl_window_.window, src, core, addr, size, noc_id, width);
}

bool PcieProtocol::write_to_core_range(
    const void* src, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, size_t size, NocId noc_id) {
    std::lock_guard<std::mutex> lock(data_write_window_.mutex);
    noc_multicast_write_reconfigure(*data_write_window_.window, src, size, core_start, core_end, addr, noc_id);
    return true;
}

int PcieProtocol::get_mmio_id() { return pci_device_->get_device_num(); }

void PcieProtocol::bar_write32(uint32_t addr, uint32_t data) { *bar_register(*pci_device_, addr) = data; }

uint32_t PcieProtocol::bar_read32(uint32_t addr) { return *bar_register(*pci_device_, addr); }

int PcieProtocol::get_numa_node() const { return pci_device_->get_numa_node(); }

std::optional<uint32_t> PcieProtocol::get_lane_count() const { return pci_device_->get_current_link_width(); }

void PcieProtocol::set_power_state(PowerState state) { pci_device_->set_power_state(state == PowerState::HIGH); }

int PcieProtocol::export_dmabuf(tt_xy_pair core, uint64_t addr, size_t size, uint64_t ordering, NocId noc_id) {
    // The smallest window that reaches the whole range from the size-aligned base below addr.
    const ArchitectureTlbs& tlbs = get_architecture_tlbs(pci_device_->get_arch());
    for (const TlbSizeClass& size_class : tlbs.size_classes) {
        const uint64_t offset_in_window = addr % size_class.size;
        if (size > size_class.size - offset_in_window) {
            continue;
        }
        tlb_data config{};
        config.local_offset = addr - offset_in_window;
        config.x_end = core.x;
        config.y_end = core.y;
        config.noc_sel = static_cast<uint64_t>(noc_id);
        config.ordering = ordering;
        config.set_static_vc(tlbs.get_static_vc(WindowFlags::Bidirectional));
        return pci_device_->export_tlb_dmabuf(size_class.size, config, offset_in_window, size);
    }
    UMD_THROW(error::RuntimeError, fmt::format("No TLB window is large enough to export {:#x} bytes.", size));
}

void PcieProtocol::set_io_timeout_callback(const std::function<bool(NocId)>& hang_check) {
    for (LockedWindow* locked_window : {&data_write_window_, &data_read_window_, &ctrl_window_}) {
        std::lock_guard<std::mutex> lock(locked_window->mutex);
        locked_window->window->set_io_timeout_hang_check(hang_check);
    }
}

}  // namespace tt::umd
