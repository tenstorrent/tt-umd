/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>

#include "umd/device/tt_device/tt_device.hpp"

namespace tt::umd {
class WormholeTTDevice : public TTDevice {
public:
    void configure_iatu_region(size_t region, uint64_t target, size_t region_size) override;

    void wait_arc_core_start(const uint32_t timeout_ms = 1000) override;

    uint32_t get_clock() override;

    uint32_t get_min_clock_freq() override;

    bool get_noc_translation_enabled() override;

    void dma_d2h(void *dst, uint32_t src, size_t size) override;

    void dma_h2d(uint32_t dst, const void *src, size_t size) override;

    void dma_h2d_zero_copy(uint32_t dst, const void *src, size_t size) override;

    void dma_d2h_zero_copy(void *dst, uint32_t src, size_t size) override;

    void read_from_arc(void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    void write_to_arc(const void *mem_ptr, uint64_t arc_addr_offset, size_t size) override;

    ChipInfo get_chip_info() override;

    uint32_t wait_eth_core_training(const tt_xy_pair eth_core, const uint32_t timeout_ms = 60000) override;

    uint64_t get_arc_noc_base_address() const override;

    tt_xy_pair get_arc_core() const;

    bool wait_arc_post_reset(const uint32_t timeout_ms = 1000) override;

    WormholeTTDevice(std::shared_ptr<PCIDevice> pci_device);
    WormholeTTDevice(std::shared_ptr<JtagDevice> jtag_device, uint8_t jlink_id);

private:
    friend std::unique_ptr<TTDevice> TTDevice::create(int device_number, IODeviceType device_type);

    void dma_d2h_transfer(const uint64_t dst, const uint32_t src, const size_t size);
    void dma_h2d_transfer(const uint32_t dst, const uint64_t src, const size_t size);

    void post_init_hook() override;

    struct EthAddresses {
        uint32_t masked_version;

        uint64_t node_info;
        uint64_t eth_conn_info;
        uint64_t results_buf;
        uint64_t erisc_remote_board_type_offset;
        uint64_t erisc_local_board_type_offset;
        uint64_t erisc_local_board_id_lo_offset;
        uint64_t erisc_remote_board_id_lo_offset;
        uint64_t erisc_remote_eth_id_offset;
    };

    static constexpr uint32_t ETH_UNKNOWN = 0;
    static constexpr uint32_t ETH_UNCONNECTED = 1;

    static EthAddresses get_eth_addresses(const uint32_t eth_fw_version);
    uint32_t read_port_status(tt_xy_pair eth_core);

    // Enforce single-threaded access, even though there are more serious issues
    // surrounding resource management as it relates to DMA.
    std::mutex dma_mutex_;
    tt_xy_pair arc_core;

    EthAddresses eth_addresses;
};
}  // namespace tt::umd
