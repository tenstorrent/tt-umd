// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0


#include "tt_device.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

tt_VersimDevice::tt_VersimDevice(const std::string &sdesc_path, const std::string &ndesc_path) : tt_device(sdesc_path) {
  throw std::runtime_error("tt_VersimDevice() -- VERSIM is not supported in this build\n");
}

tt_VersimDevice::~tt_VersimDevice () {}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_VersimDevice::get_virtual_soc_descriptors() {
    throw std::runtime_error("tt_VersimDevice() -- VERSIM is not supported in this build\n");
    return soc_descriptor_per_chip;
}

int tt_VersimDevice::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_VersimDevice::get_all_chips_in_cluster() { return {}; }
int tt_VersimDevice::detect_number_of_chips() { return 0; }

void tt_VersimDevice::start_device(const tt_device_params &device_params) {}
void tt_VersimDevice::close_device() {}
void tt_VersimDevice::write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {}
void tt_VersimDevice::broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb) {}
void tt_VersimDevice::read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {}
void tt_VersimDevice::rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {}
void tt_VersimDevice::write_to_device(const void *mem_ptr, uint32_t len, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {}
void tt_VersimDevice::read_from_device(void *mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {}
void tt_VersimDevice::rolled_write_to_device(uint32_t* mem_ptr, uint32_t len, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {}
void tt_VersimDevice::wait_for_non_mmio_flush() {}

void tt_VersimDevice::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {}
void tt_VersimDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {}
void tt_VersimDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& dram_cores) {}

void tt_VersimDevice::start(
    std::vector<std::string> plusargs,
    std::vector<std::string> dump_cores,
    bool no_checkers,
    bool /*init_device*/,
    bool /*skip_driver_allocs*/
) {}

void tt_VersimDevice::deassert_risc_reset() {}
void tt_VersimDevice::deassert_risc_reset_at_core(tt_cxy_pair core) {}
void tt_VersimDevice::assert_risc_reset() {}
void tt_VersimDevice::assert_risc_reset_at_core(tt_cxy_pair core) {}

void tt_VersimDevice::translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {};
// void tt_VersimDevice::dump_wall_clock_mailbox(std::string output_path, int device_id) {}

std::set<chip_id_t> tt_VersimDevice::get_target_mmio_device_ids() {return {};}
std::set<chip_id_t> tt_VersimDevice::get_target_remote_device_ids() {return {};}

bool versim_check_dram_core_exists(
    const std::vector<std::vector<tt_xy_pair>> &dram_core_channels, tt_xy_pair target_core) {
  return false;
}

bool tt_VersimDevice::using_harvested_soc_descriptors() { return false; }
bool tt_VersimDevice::noc_translation_en() { return false; }
std::unordered_map<chip_id_t, uint32_t> tt_VersimDevice::get_harvesting_masks_for_soc_descriptors() { return std::unordered_map<chip_id_t, uint32_t>();}

bool tt_VersimDevice::stop() { return true; }

void tt_VersimDevice::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {}
void tt_VersimDevice::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {}

std::uint32_t tt_VersimDevice::get_num_dram_channels(std::uint32_t device_id) {return 0;}
std::uint64_t tt_VersimDevice::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {return 0;}
std::uint32_t tt_VersimDevice::get_num_host_channels(std::uint32_t device_id) {return 0;}
std::uint32_t tt_VersimDevice::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {return 0;}

std::map<int,int> tt_VersimDevice::get_clocks() {return std::map<int,int>();}

tt_ClusterDescriptor* tt_VersimDevice::get_cluster_description() {return ndesc.get();}

