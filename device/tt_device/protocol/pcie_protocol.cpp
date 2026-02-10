/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "umd/device/tt_device/protocol/pcie_protocol.hpp"

namespace tt::umd {

PcieProtocol::PcieProtocol(
    std::shared_ptr<PCIDevice> pci_device, architecture_implementation *architecture_impl, bool use_safe_api) :
    pci_device_(std::move(pci_device)),
    communication_device_id_(pci_device_->get_device_num()),
    architecture_impl_(architecture_impl),
    use_safe_api_(use_safe_api) {}

void PcieProtocol::write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (use_safe_api_) {
        write_to_device_impl<true>(mem_ptr, core, addr, size);
        return;
    }
    write_to_device_impl<false>(mem_ptr, core, addr, size);
}

void PcieProtocol::read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if (use_safe_api_) {
        read_from_device_impl<true>(mem_ptr, core, addr, size);
        return;
    }
    read_from_device_impl<false>(mem_ptr, core, addr, size);
}

template <bool safe>
void PcieProtocol::read_from_device_impl(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if constexpr (safe) {
        get_cached_tlb_window()->safe_read_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->read_block_reconfigure(mem_ptr, core, addr, size);
    }
}

template <bool safe>
void PcieProtocol::write_to_device_impl(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) {
    if constexpr (safe) {
        get_cached_tlb_window()->safe_write_block_reconfigure(mem_ptr, core, addr, size);
    } else {
        get_cached_tlb_window()->write_block_reconfigure(mem_ptr, core, addr, size);
    }
}

std::shared_ptr<PCIDevice> PcieProtocol::get_pci_device() { return pci_device_; }

TlbWindow *PcieProtocol::get_cached_tlb_window() {
    if (cached_tlb_window == nullptr) {
        cached_tlb_window = std::make_unique<TlbWindow>(
            get_pci_device()->allocate_tlb(architecture_impl_->get_cached_tlb_size(), TlbMapping::UC));
        return cached_tlb_window.get();
    }
    return cached_tlb_window.get();
}

TlbWindow *PcieProtocol::get_cached_pcie_dma_tlb_window(tlb_data config) {
    if (cached_pcie_dma_tlb_window == nullptr) {
        cached_pcie_dma_tlb_window =
            std::make_unique<TlbWindow>(get_pci_device()->allocate_tlb(16 * 1024 * 1024, TlbMapping::WC), config);
        return cached_pcie_dma_tlb_window.get();
    }

    cached_pcie_dma_tlb_window->configure(config);
    return cached_pcie_dma_tlb_window.get();
}

}  // namespace tt::umd
