/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <mutex>

#include "umd/device/tt_device/protocol/device_protocol.hpp"
#include "umd/device/tt_device/protocol/pcie_interface.hpp"
#include "umd/device/utils/lock_manager.hpp"

namespace tt::umd {

class PCIDevice;
class TlbWindow;
struct tlb_data;

/**
 * PcieProtocol implements DeviceProtocol and PcieInterface for PCIe-connected devices.
 *
 * Handles all device I/O through PCIe TLB windows, including DMA transfers,
 * register access, and multicast writes.
 *
 * Method implementations will be migrated from TTDevice in subsequent PRs.
 */
class PcieProtocol : public DeviceProtocol, public PcieInterface {
public:
    explicit PcieProtocol(
        std::shared_ptr<PCIDevice> pci_device, architecture_implementation* architecture_impl, bool use_safe_api);

    ~PcieProtocol() override = default;

    // DeviceProtocol interface.
    void write_to_device(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    void read_from_device(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size) override;
    tt::ARCH get_arch() override;
    architecture_implementation* get_architecture_implementation() override;
    int get_communication_device_id() const override;
    IODeviceType get_communication_device_type() override;
    void detect_hang_read(uint32_t data_read = HANG_READ_VALUE) override;
    bool is_hardware_hung() override;

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
        void* dst, size_t size, tt_xy_pair core_start, tt_xy_pair core_end, uint64_t addr) override;
    void write_regs(volatile uint32_t* dest, const uint32_t* src, uint32_t word_len) override;
    void bar_write32(uint32_t addr, uint32_t data) override;
    uint32_t bar_read32(uint32_t addr) override;

private:
    template <bool safe>
    void write_to_device_impl(const void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    template <bool safe>
    void read_from_device_impl(void* mem_ptr, tt_xy_pair core, uint64_t addr, uint32_t size);

    TlbWindow* get_cached_tlb_window();
    TlbWindow* get_cached_pcie_dma_tlb_window(tlb_data config);

    void dma_h2d_transfer(uint32_t dst, uint64_t src, size_t size);
    void dma_d2h_transfer(uint64_t dst, uint32_t src, size_t size);

    std::shared_ptr<PCIDevice> pci_device_;
    int communication_device_id_;
    architecture_implementation* architecture_impl_;
    std::unique_ptr<TlbWindow> cached_tlb_window;
    std::unique_ptr<TlbWindow> cached_pcie_dma_tlb_window;
    bool use_safe_api_;
    std::mutex pcie_io_lock;
    std::mutex dma_mutex_;
    LockManager lock_manager;
};

}  // namespace tt::umd
