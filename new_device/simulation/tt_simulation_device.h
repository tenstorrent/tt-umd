/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <vector>

#include "new_device/chip.h"
#include "tt_simulation_device_generated.h"
#include "new_device/simulation/tt_simulation_host.hpp"

using namespace tt::umd;

class tt_SimulationDevice: public Chip {
    public:
    tt_SimulationDevice(const std::string &sdesc_path);
    ~tt_SimulationDevice();

    tt_SimulationHost host;

    //Setup/Teardown Functions
    virtual std::unordered_map<chip_id_t, SocDescriptor>& get_virtual_soc_descriptors();
    virtual void set_device_l1_address_params(const device_l1_address_params& l1_address_params_);
    virtual void set_device_dram_address_params(const device_dram_address_params& dram_address_params_);
    virtual void set_driver_host_address_params(const driver_host_address_params& host_address_params_);
    virtual void set_driver_eth_interface_params(const driver_eth_interface_params& eth_interface_params_);
    virtual void start_device(const device_params &device_params);
    virtual void assert_risc_reset();
    virtual void deassert_risc_reset();
    virtual void deassert_risc_reset_at_core(cxy_pair core);
    virtual void assert_risc_reset_at_core(cxy_pair core);
    virtual void close_device();

    // Runtime Functions
    virtual void write_to_device(const void *mem_ptr, uint32_t size_in_bytes, cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    virtual void read_from_device(void* mem_ptr, cxy_pair core, uint64_t addr, uint32_t size, const std::string& fallback_tlb);
    virtual void write_to_sysmem(std::vector<uint32_t>& vec, uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void write_to_sysmem(const void* mem_ptr, std::uint32_t size,  uint64_t addr, uint16_t channel, chip_id_t src_device_id);
    virtual void read_from_sysmem(std::vector<uint32_t> &vec, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    virtual void read_from_sysmem(void* mem_ptr, uint64_t addr, uint16_t channel, uint32_t size, chip_id_t src_device_id);
    
    virtual void wait_for_non_mmio_flush(); //
    void l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<xy_pair>& cores = {});
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<xy_pair>& cores = {});


    // Misc. Functions to Query/Set Device State
    // virtual bool using_harvested_soc_descriptors();
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    static std::vector<chip_id_t> detect_available_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual std::map<int,int> get_clocks();
    virtual void *host_dma_address(std::uint64_t offset, chip_id_t src_device_id, uint16_t channel) const;
    virtual std::uint64_t get_pcie_base_addr_from_device() const;
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id);
    virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_numa_node_for_pcie_device(std::uint32_t device_id);

    private:
    // State variables
    device_dram_address_params dram_address_params;
    device_l1_address_params l1_address_params;
    driver_host_address_params host_address_params;
    driver_eth_interface_params eth_interface_params;
    std::vector<Arch> archs_in_cluster = {};
    std::set<chip_id_t> target_devices_in_cluster = {};
    std::set<chip_id_t> target_remote_chips = {};
    Arch arch_name;
    std::shared_ptr<ClusterDescriptor> ndesc;

    flatbuffers::FlatBufferBuilder create_flatbuffer(DEVICE_COMMAND rw, std::vector<uint32_t> vec, cxy_pair core_, uint64_t addr, uint64_t size_=0);
    void print_flatbuffer(const DeviceRequestResponse *buf);
};
