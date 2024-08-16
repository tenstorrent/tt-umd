/*
 * SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "tt_device.h"
#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"

class c_versim_core;
namespace nuapi {namespace device {template <typename, typename>class Simulator;}}
namespace versim {
  struct VersimSimulatorState;
  using VersimSimulator = nuapi::device::Simulator<c_versim_core *, VersimSimulatorState>;
}

/**
 * @brief Versim Backend Class, derived from the tt_device class
 * Implements APIs to communicate with a simulated (using Verilator) Tenstorrent Device.
*/ 
class tt_VersimDevice: public tt_device
{
    public:
    virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_);
    virtual void set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_);
    tt_VersimDevice(const std::string &sdesc_path, const std::string &ndesc_path);
    virtual std::unordered_map<chip_id_t, tt_SocDescriptor>& get_virtual_soc_descriptors();
    virtual void start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool init_device, bool skip_driver_allocs);
    virtual void start_device(const tt_device_params &device_params);
    virtual void close_device();
    virtual void deassert_risc_reset();
    virtual void deassert_risc_reset_at_core(tt_cxy_pair core);
    virtual void assert_risc_reset();
    virtual void assert_risc_reset_at_core(tt_cxy_pair core);
    virtual void write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    virtual void broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& columns_to_exclude, const std::string& fallback_tlb);
    virtual void rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use);
    virtual void read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);
    virtual void rolled_write_to_device(uint32_t* mem_ptr, uint32_t size_in_bytes, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb);
    virtual void write_to_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true, bool ordered_with_prev_remote_write = false);
    virtual void read_from_device(void *mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use); 
    virtual void wait_for_non_mmio_flush();
    void l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels);
    void dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores = {});
    virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c);
    virtual bool using_harvested_soc_descriptors();
    virtual std::unordered_map<chip_id_t, uint32_t> get_harvesting_masks_for_soc_descriptors();
    virtual bool noc_translation_en();
    virtual std::set<chip_id_t> get_target_mmio_device_ids();
    virtual std::set<chip_id_t> get_target_remote_device_ids();
    virtual ~tt_VersimDevice();
    virtual tt_ClusterDescriptor* get_cluster_description();
    virtual int get_number_of_chips_in_cluster();
    virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster();
    static int detect_number_of_chips();
    virtual std::map<int,int> get_clocks();
    virtual std::uint32_t get_num_dram_channels(std::uint32_t device_id);
    virtual std::uint64_t get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel);
    virtual std::uint32_t get_num_host_channels(std::uint32_t device_id);
    virtual std::uint32_t get_host_channel_size(std::uint32_t device_id, std::uint32_t channel);
    private:
    bool stop();
    tt_device_l1_address_params l1_address_params;
    tt_device_dram_address_params dram_address_params;
    versim::VersimSimulator* versim;
    std::shared_ptr<tt_ClusterDescriptor> ndesc;
    void* p_ca_soc_manager;
};
