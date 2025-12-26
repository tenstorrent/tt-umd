// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <set>

#include "umd/device/arc/blackhole_arc_telemetry_reader.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class BlackholeTTDevice : public TTDevice {
public:
    ~BlackholeTTDevice();

    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    bool wait_arc_core_start(const std::chrono::milliseconds timeout_ms = timeout::ARC_STARTUP_TIMEOUT) override;

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

protected:
    BlackholeTTDevice(std::shared_ptr<PCIDevice> pci_device, bool use_safe_api);
    BlackholeTTDevice(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id);

    bool is_hardware_hung() override;

    virtual bool is_arc_available_over_axi();

private:
    int get_pcie_x_coordinate();

    friend std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type, bool use_safe_api);

    static constexpr uint64_t ATU_OFFSET_IN_BH_BAR2 = 0x1000;
    std::set<size_t> iatu_regions_;
};

}  // namespace tt::umd
