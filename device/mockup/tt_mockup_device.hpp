/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

#include "umd/device/cluster.h"
#include "umd/device/tt_cluster_descriptor.h"

class tt_MockupDevice : public tt_device {
public:
    tt_MockupDevice(const std::string& sdesc_path) : tt_device() {
        soc_descriptor_per_chip.emplace(0, tt_SocDescriptor(sdesc_path, false));
        target_devices_in_cluster = {0};
    }

    virtual ~tt_MockupDevice() {}

    void set_barrier_address_params(const barrier_address_params& barrier_address_params_) override {}

    void configure_active_ethernet_cores_for_mmio_device(
        chip_id_t mmio_chip, const std::unordered_set<tt::umd::CoreCoord>& active_eth_cores_per_chip) override {}

    void start_device(const tt_device_params& device_params) override {}

    void assert_risc_reset() override {}

    void deassert_risc_reset() override {}

    void deassert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_DEASSERT_SOFT_RESET) override {}

    void assert_risc_reset_at_core(
        const chip_id_t chip,
        const tt::umd::CoreCoord core,
        const TensixSoftResetOptions& soft_resets = TENSIX_ASSERT_SOFT_RESET) override {}

    void close_device() override {}

    void wait_for_non_mmio_flush() override {}

    void wait_for_non_mmio_flush(const chip_id_t chip_id) override {}

    void write_to_device(
        const void* mem_ptr, uint32_t size_in_bytes, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) override {}

    void broadcast_write_to_cluster(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        uint64_t address,
        const std::set<chip_id_t>& chips_to_exclude,
        std::set<uint32_t>& rows_to_exclude,
        std::set<uint32_t>& columns_to_exclude) override {}

    void read_from_device(
        void* mem_ptr, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr, uint32_t size) override {}

    void dma_write_to_device(
        const void* src, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) override {}

    void dma_read_from_device(void* dst, size_t size, chip_id_t chip, tt::umd::CoreCoord core, uint64_t addr) override {
    }

    void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) override {}

    void read_from_sysmem(
        void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) override {}

    void l1_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores = {}) override {}

    void dram_membar(const chip_id_t chip, const std::unordered_set<uint32_t>& channels = {}) override {}

    void dram_membar(const chip_id_t chip, const std::unordered_set<tt::umd::CoreCoord>& cores = {}) override {}

    int arc_msg(
        int logical_device_id,
        uint32_t msg_code,
        bool wait_for_done = true,
        uint32_t arg0 = 0,
        uint32_t arg1 = 0,
        uint32_t timeout_ms = 1000,
        uint32_t* return_3 = nullptr,
        uint32_t* return_4 = nullptr) override {
        return 0;
    }

    tt_ClusterDescriptor* get_cluster_description() override { return cluster_descriptor.get(); }

    std::set<chip_id_t> get_target_device_ids() override { return target_devices_in_cluster; }

    std::set<chip_id_t> get_target_mmio_device_ids() override { return target_devices_in_cluster; }

    std::set<chip_id_t> get_target_remote_device_ids() override { return target_remote_chips; }

    std::map<int, int> get_clocks() override { return {{0, 0}}; }

    std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id) override { return 0; }

    tt_version get_ethernet_fw_version() const override { return {0, 0, 0}; }

    std::uint32_t get_num_host_channels(std::uint32_t device_id) override { return 1; }

    std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) override { return 0; }

    void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const override {
        return nullptr;
    }

    std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const override { return 0; }

    const tt_SocDescriptor& get_soc_descriptor(chip_id_t chip_id) const override {
        return soc_descriptor_per_chip.at(chip_id);
    };

private:
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    std::shared_ptr<tt_ClusterDescriptor> cluster_descriptor;
    std::unordered_map<chip_id_t, tt_SocDescriptor> soc_descriptor_per_chip = {};
};
