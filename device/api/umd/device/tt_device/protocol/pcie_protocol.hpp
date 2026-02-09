/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "device_protocol.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"

namespace tt::umd {

class PcieProtocol : public DeviceProtocol {
public:
    PcieProtocol(std::shared_ptr<PCIDevice> pci_device, bool use_safe_api);

    virtual ~PcieProtocol() = default;

    void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    std::shared_ptr<PCIDevice> get_pci_device();

private:
    template <bool safe>
    void write_to_device_impl(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    template <bool safe>
    void read_from_device_impl(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    TlbWindow *get_cached_tlb_window();

    TlbWindow *get_cached_pcie_dma_tlb_window(tlb_data config);

    std::unique_ptr<TlbWindow> cached_tlb_window = nullptr;

    std::unique_ptr<TlbWindow> cached_pcie_dma_tlb_window = nullptr;

    std::shared_ptr<PCIDevice> pci_device_;

    bool use_safe_api_ = false;
};

}  // namespace tt::umd
