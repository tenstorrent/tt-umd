// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class WormholeTTDevice : public TTDevice {
public:
    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    bool wait_arc_core_start(
        const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) noexcept override;

    uint32_t get_clock() override;

    uint32_t get_min_clock_freq() override;

    bool get_noc_translation_enabled() override;

    void dma_d2h(void *dst, uint32_t src, size_t size) override;

    void dma_h2d(uint32_t dst, const void *src, size_t size) override;

    void dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) override;

    void dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) override;

    void read_from_arc_apb(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_apb(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void read_from_arc_csm(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc_csm(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    ChipInfo get_chip_info() override;

    std::chrono::milliseconds wait_eth_core_training(
        const tt_xy_pair eth_core, const std::chrono::milliseconds timeout_ms = timeout::ETH_TRAINING_TIMEOUT) override;

    void l1_membar(const std::unordered_set<tt_xy_pair> &cores = {}) override;

    ~WormholeTTDevice() override = default;

protected:
    WormholeTTDevice(std::shared_ptr<PCIDevice> pci_device);
    WormholeTTDevice(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id);
    /*
     * Create a device without an underlying communication device.
     * Used for remote devices that depend on remote_communication.
     * WARNING: This constructor should not be used for PCIe devices as certain functionalities from base class rely on
     * the presence of an underlying communication device. Creating a WormholeTTDevice without an underlying
     * communication device over PCIe would require overriding several methods from the base class.
     */
    WormholeTTDevice();

private:
    friend std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type);

    void dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size);
    void dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size);

    bool is_hardware_hung() override;

    void set_membar_flag(
        const std::vector<tt_xy_pair> &cores, const uint32_t barrier_value, const uint32_t barrier_addr);
    void insert_host_to_device_barrier(const std::vector<tt_xy_pair> &cores, const uint32_t barrier_addr);

    static constexpr uint32_t LINK_TRAIN_TRAINING = 0;

    uint32_t read_training_status(tt_xy_pair eth_core);

    // Enforce single-threaded access, even though there are more serious issues
    // surrounding resource management as it relates to DMA.
    std::mutex dma_mutex_;
};
}  // namespace tt::umd
