/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <mutex>
#include <optional>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_dma/dma_transfer.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {

class PCIDevice;
class TlbWindow;
struct tlb_data;

/**
 * PcieProtocol implements DeviceProtocol and PcieInterface for PCIe-connected devices.
 *
 * Provides PCIe-based device I/O including DMA transfers, register access,
 * and multicast writes.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface {
public:
    explicit PcieProtocol(std::unique_ptr<PCIDevice> pci_device, bool use_safe_api = false);

    ~PcieProtocol() override;

    // DeviceProtocol interface.
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    bool write_to_core_range(
        const void* mem_ptr, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr, uint32_t size) override;

    // PcieInterface.
    PCIDevice* get_pci_device() override;
    [[nodiscard]] bool dma_write_to_device(void* src, size_t size, tt_xy_pair core, uint64_t addr) override;
    [[nodiscard]] bool dma_read_from_device(void* dst, size_t size, tt_xy_pair core, uint64_t addr) override;
    [[nodiscard]] bool dma_multicast_write(
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
    TlbWindow* get_cached_tlb_window();
    TlbWindow* get_cached_dma_tlb_window(tlb_data config);

    static DmaTransferStrategy create_dma_strategy(tt::ARCH arch);
    static size_t get_dma_tlb_size(tt::ARCH arch);

    void dma_d2h_transfer(uint64_t dst, uint32_t src, size_t size);
    void dma_h2d_transfer(uint32_t dst, uint64_t src, size_t size);

    enum class DmaDirection { H2D, D2H };
    tlb_data create_dma_tlb_config(
        uint64_t addr, tt_xy_pair core_end, std::optional<tt_xy_pair> core_start = std::nullopt);
    bool dma_transfer(void* buffer, size_t size, uint64_t addr, tlb_data config, DmaDirection direction);

    template <bool safe>
    void write_to_device_impl(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);
    template <bool safe>
    void read_from_device_impl(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    // Offset used to access NOC2AXI config + ARC specific memory (ICCM + CSM + APB).
    static constexpr uint32_t BAR0_OFFSET = 0x1FD00000;

    std::unique_ptr<PCIDevice> pci_device_;
    DmaTransferStrategy dma_strategy_;
    bool use_safe_api_;
    std::mutex io_lock_;
    std::mutex dma_mutex_;
    std::unique_ptr<TlbWindow> cached_tlb_window_;
    std::unique_ptr<TlbWindow> cached_dma_tlb_window_;
};

}  // namespace tt::umd
