/*
 * SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/pcie/tlb_window.hpp"
#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

class PcieProtocol final : public DeviceProtocol, public PcieInterface {
public:
    explicit PcieProtocol(
        std::shared_ptr<PCIDevice> pci_device, architecture_implementation *architecture_impl, bool use_safe_api);

    PcieProtocol() = delete;

    /* DeviceProtocol */
    virtual ~PcieProtocol() = default;

    void write_to_device(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    void read_from_device(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;

    /* MmioProtocol */
    tt::ARCH get_arch() override;

    int get_communication_device_id() const override;

    IODeviceType get_communication_device_type() override;

    architecture_implementation *get_architecture_implementation() override;

    void detect_hang_read(uint32_t data_read = HANG_READ_VALUE) override;

    bool is_hardware_hung() override;

    /* PcieInterface */
    PCIDevice *get_pci_device() override;

    void dma_write_to_device(const void *src, size_t size, tt_xy_pair core, uint64_t addr) override;

    void dma_read_from_device(void *dst, size_t size, tt_xy_pair core, uint64_t addr) override;

    void dma_multicast_write(
        void *src, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void dma_d2h(void *dst, uint32_t src, size_t size) override;

    void dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) override;

    void dma_h2d(uint32_t dst, const void *src, size_t size) override;

    void dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) override;

    /* Need this for RemoteProtocol also */
    void noc_multicast_write(
        void *dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;

    void write_regs(volatile uint32_t *dest, const uint32_t *src, uint32_t word_len) override;

    void bar_write32(uint32_t addr, uint32_t data) override;

    uint32_t bar_read32(uint32_t addr) override;

private:
    template <bool safe>
    void write_to_device_impl(const void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    template <bool safe>
    void read_from_device_impl(void *mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    TlbWindow *get_cached_tlb_window();

    TlbWindow *get_cached_pcie_dma_tlb_window(tlb_data config);

    void dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size);

    void dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size);

    std::mutex pcie_io_lock;

    // Enforce single-threaded access, even though there are more serious issues
    // surrounding resource management as it relates to DMA.
    std::mutex dma_mutex_;

    LockManager lock_manager;

    std::shared_ptr<PCIDevice> pci_device_;

    int communication_device_id_ = -1;

    architecture_implementation *architecture_impl_ = nullptr;

    std::unique_ptr<TlbWindow> cached_tlb_window = nullptr;

    std::unique_ptr<TlbWindow> cached_pcie_dma_tlb_window = nullptr;

    bool use_safe_api_ = false;
};

}  // namespace tt::umd
