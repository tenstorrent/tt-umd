#include <stdexcept>
#include <cstring>

#include "common/logger.hpp"
#include "tt_emulation_device.h"

tt_emulation_device::tt_emulation_device(const std::string& sdesc_path) : tt_device(sdesc_path) {
  throw std::runtime_error("tt_emulation_device() -- Zebu Emulation is not supported in this build\n");
}


tt_emulation_device::~tt_emulation_device() {}
  
void tt_emulation_device::write(tt_cxy_pair core, uint64_t addr, const std::vector<uint8_t>& data) {}

std::vector<uint8_t> tt_emulation_device::read(tt_cxy_pair core, uint64_t addr, uint32_t size) {return {};}


void tt_emulation_device::start_device(const tt_device_params& device_params) {}

void tt_emulation_device::deassert_risc_reset() {}

void tt_emulation_device::assert_risc_reset() {}

void tt_emulation_device::deassert_risc_reset_at_core(tt_cxy_pair core) {}

void tt_emulation_device::assert_risc_reset_at_core(tt_cxy_pair core) {}

void tt_emulation_device::close_device() {}

void tt_emulation_device::start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool /*init_device*/, bool /*skip_driver_allocs*/) {}


void tt_emulation_device::broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb) {} 
void tt_emulation_device::rolled_write_to_device(std::vector<uint32_t>& base_vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t base_addr, const std::string& tlb_to_use) {}

void tt_emulation_device::write_to_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {}
void tt_emulation_device::write_to_device(const void *mem_ptr, uint32_t size_in_bytes, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {};
void tt_emulation_device::read_from_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& /*tlb_to_use*/) {}
void tt_emulation_device::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {}
void tt_emulation_device::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {}
void tt_emulation_device::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {}


// -------------------------
// Not sure how to implement these functions below, leaving them blank/default for now
void tt_emulation_device::translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c) {
  // No translation is performed
  return;
}
tt_ClusterDescriptor* tt_emulation_device::get_cluster_description() { return ndesc.get(); }

std::set<chip_id_t> tt_emulation_device::get_target_mmio_device_ids() {return {};}

std::set<chip_id_t> tt_emulation_device::get_target_remote_device_ids() {return {};}

void tt_emulation_device::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {}
int tt_emulation_device::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_emulation_device::get_all_chips_in_cluster() { return { 0 }; }
int tt_emulation_device::detect_number_of_chips() { return 1; }

bool tt_emulation_device::using_harvested_soc_descriptors() { return false; }
bool tt_emulation_device::noc_translation_en() { return false; }
std::unordered_map<chip_id_t, uint32_t> tt_emulation_device::get_harvesting_masks_for_soc_descriptors() { return {{0, 0}};}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_emulation_device::get_virtual_soc_descriptors() {return soc_descriptor_per_chip;}

std::map<int, int> tt_emulation_device::get_clocks() {return std::map<int, int>();}

void tt_emulation_device::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {}



