/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"

namespace tt::umd {

class PCIDevice;

/**
 * PcieProtocol implements DeviceProtocol and PcieInterface for PCIe-connected devices.
 *
 * Handles all device I/O through PCIe TLB windows, including DMA transfers,
 * register access, and multicast writes.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface {
public:
    explicit PcieProtocol(std::shared_ptr<PCIDevice> pci_device);

    ~PcieProtocol() override = default;

    // DeviceProtocol interface.
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    bool write_to_device_range(
        const void* mem_ptr, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint32_t size) override;

    // PcieInterface.
    PCIDevice* get_pci_device() override;
    void dma_write_to_device(const void* src, size_t size, tt_xy_pair core, uint64_t addr) override;
    void dma_read_from_device(void* dst, size_t size, tt_xy_pair core, uint64_t addr) override;
    void dma_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;
    void dma_d2h(void* dst, uint32_t src, size_t size) override;
    void dma_d2h_zero_copy(void* dst, uint32_t src, size_t size) override;
    void dma_h2d(uint32_t dst, const void* src, size_t size) override;
    void dma_h2d_zero_copy(uint32_t dst, const void* src, size_t size) override;
    void noc_multicast_write(
        void* src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;
    void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) override;
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;

private:
    std::shared_ptr<PCIDevice> pci_device_;
};

}  // namespace tt::umd
