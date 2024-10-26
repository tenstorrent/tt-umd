/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <vector>

#include "device/tt_cluster_descriptor.h"
#include "device/tt_device.h"

class tt_MockupDevice : public tt_device {
   public:
    tt_MockupDevice(const std::string& sdesc_path) : tt_device(sdesc_path) {
        soc_descriptor_per_chip.emplace(0, tt_SocDescriptor(sdesc_path));
        std::set<chip_id_t> target_devices = {0};
    }
    virtual ~tt_MockupDevice() {}

    // Setup/Teardown Functions
    std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors() override {
        return soc_descriptor_per_chip;
    }
    void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) override {}
    void set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) override {}
    void set_driver_host_address_params(const tt_driver_host_address_params& host_address_params_) override {}
    void set_driver_eth_interface_params(
        const tt_driver_eth_interface_params& eth_interface_params_) override {}
    void start_device(const tt_device_params& device_params) override {}
    void assert_risc_reset() override {}
    void deassert_risc_reset() override {}
    void deassert_risc_reset_at_core(tt_cxy_pair core) override {}
    void assert_risc_reset_at_core(tt_cxy_pair core) override {}
    void close_device() override {}

    // Runtime Functions
    void write_to_device(
        const void* mem_ptr,
        uint32_t size_in_bytes,
        tt_cxy_pair core,
        uint64_t addr,
        const std::string& tlb_to_use) override {}
    void write_to_device(
        std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) override {}
    void read_from_device(
        void* mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb) override {}
    void read_from_device(
        std::vector<uint32_t>& vec,
        tt_cxy_pair core,
        uint64_t addr,
        uint32_t size,
        const std::string& tlb_to_use) override {}
    void write_to_sysmem(
        std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id) override {}
    void write_to_sysmem(
        const void* mem_ptr, std::uint32_t size, uint64_t addr, uint16_t channel, chip_id_t src_device_id) override {}
    void read_from_sysmem(
        std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) override {}
    void read_from_sysmem(
        void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id) override {}

    void l1_membar(
        const chip_id_t chip,
        const std::string& fallback_tlb,
        const std::unordered_set<tt_xy_pair>& cores = {}) override {}
    void dram_membar(
        const chip_id_t chip,
        const std::string& fallback_tlb,
        const std::unordered_set<uint32_t>& channels = {}) override {}
    void dram_membar(
        const chip_id_t chip,
        const std::string& fallback_tlb,
        const std::unordered_set<tt_xy_pair>& cores = {}) override {}

    void wait_for_non_mmio_flush() override {}

    // Misc. Functions to Query/Set Device State
    std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors() override {
        return {{0, 0}};
    }
    static std::vector<chip_id_t> detect_available_device_ids() { return {0}; };
    std::set<chip_id_t> get_target_remote_device_ids() override { return target_remote_chips; }
    std::map<int, int> get_clocks() override { return {{0, 0}}; }
    void* host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const override {
        return nullptr;
    }
    std::uint64_t get_pcie_base_addr_from_device(const chip_id_t chip_id) const override { return 0; }
    std::uint32_t get_num_dram_channels(std::uint32_t device_id) override {
        return get_soc_descriptor(device_id)->get_num_dram_channels();
    };
    std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) override {
        return get_soc_descriptor(device_id)->dram_bank_size;
    }
    std::uint32_t get_num_host_channels(std::uint32_t device_id) override { return 1; }
    std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) override { return 0; }
    std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id) override { return 0; }

   private:
    std::vector<tt::ARCH> archs_in_cluster = {};
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    tt::ARCH arch_name;
    std::shared_ptr<tt_ClusterDescriptor> ndesc;
};
