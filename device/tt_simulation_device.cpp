#include <stdexcept>
#include <cstring>

#include "common/logger.hpp"
#include "device/tt_cluster_descriptor.h"
#include "tt_simulation_device.h"

#include "tt_vcs_wrapper.h"



tt_simulation_device::tt_simulation_device(const std::string& sdesc_path) : tt_device(sdesc_path) {
  soc_descriptor_per_chip.emplace(0, tt_SocDescriptor(sdesc_path));
  std::set<chip_id_t> target_devices = {0};
  // create just a default one, we do not have cluster anyway
  ndesc = tt_ClusterDescriptor::create_for_grayskull_cluster(target_devices, {});
  tt_vcs_wrapper_inst = new tt_vcs_wrapper();

  log_info(tt::LogSimulationDriver, "Created Simulation Device ");
}

tt_simulation_device::~tt_simulation_device() {
  ndesc.reset();
  delete tt_vcs_wrapper_inst;

  log_info(tt::LogSimulationDriver, "Destroyed Simulation Device ");
}
  
void tt_simulation_device::write(tt_cxy_pair core, uint64_t addr, const std::vector<uint8_t>& data) {
  const uint32_t size = static_cast<uint32_t>(data.size());
  
  tt_vcs_wrapper_inst->axi_write(0, core.x, core.y, addr, size, data);

  log_info(tt::LogSimulationDriver, "Wrote {} bytes to address {:#016x}, core {},{}", size, addr, core.x, core.y);
}

std::vector<uint8_t> tt_simulation_device::read(tt_cxy_pair core, uint64_t addr, uint32_t size) {
  std::vector<uint8_t> data(size);

  tt_vcs_wrapper_inst->axi_read(0, core.x, core.y, addr, size, data);

  log_info(tt::LogSimulationDriver, "Read {} bytes from address {:#016x}, core {},{}", size, addr, core.x, core.y);

  return data;
}


void tt_simulation_device::start_device(const tt_device_params& device_params) {

  //tt_vcs_wrapper_inst->enable_axi_transaction_prints(true);

  log_info(tt::LogSimulationDriver, "Started Simulation Device ");
}

void tt_simulation_device::deassert_risc_reset() {
  tt_vcs_wrapper_inst->all_tensix_reset_deassert();
  log_info(tt::LogSimulationDriver, "Deasserted all tensix RISC Reset ");
}

void tt_simulation_device::assert_risc_reset() {
  tt_vcs_wrapper_inst->all_tensix_reset_assert();
  log_info(tt::LogSimulationDriver, "Asserted all tensix RISC Reset ");
}

void tt_simulation_device::deassert_risc_reset_at_core(tt_cxy_pair core) {
}

void tt_simulation_device::assert_risc_reset_at_core(tt_cxy_pair core) {
}



void tt_simulation_device::close_device() {
    log_info(tt::LogSimulationDriver, "Closing Simulation Device ");

}

void tt_simulation_device::start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool /*init_device*/, bool /*skip_driver_allocs*/
) {
  log_info(tt::LogSimulationDriver, "Starting Simulation Device ");
}


void tt_simulation_device::broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb) {
  for(const auto& core : get_soc_descriptor(0) -> cores) {
    // if(cols_to_exclude.find(core.first.x) == cols_to_exclude.end() and rows_to_exclude.find(core.first.y) == rows_to_exclude.end() and core.second.type != CoreType::HARVESTED) {
    //     write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(0, core.first.x, core.first.y), address, "");
    //   }
    // MT: Iterate through all the worker cores for bcast:
    // if (get_soc_descriptor(0)->is_worker_core(core.first)) {
    //   write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(0, core.first.x, core.first.y), address, "");
    // }
    // Simulation only broadcasts to all Tensix cores or all DRAM cores.
    // differentiate which bcast pattern to use based on exclude columns
    if (cols_to_exclude.find(0) == cols_to_exclude.end()) {
      // Detect DRAM bcast
      if (get_soc_descriptor(0)->is_dram_core(core.first)) {
        write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(0, core.first.x, core.first.y), address, "");
      }
    } else {
      if (get_soc_descriptor(0)->is_worker_core(core.first)) {
        write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(0, core.first.x, core.first.y), address, "");
      }
    }
  }
} 
void tt_simulation_device::rolled_write_to_device(std::vector<uint32_t>& base_vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t base_addr, const std::string& tlb_to_use) {
  std::vector<uint32_t> vec = base_vec;
  uint32_t byte_increment = 4 * vec.size();
  for (uint32_t i = 0; i < unroll_count; ++i) {
    vec[0] = i; // slot id for debug
    uint64_t offset_addr = base_addr + i * byte_increment;
    write_to_device(vec, core, offset_addr, tlb_to_use);
  }
}
void tt_simulation_device::write_to_device(const void *mem_ptr, uint32_t size, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
  log_assert(!(size % 4), "Writes to Simulation Backend should be 4 byte aligned!");

  std::vector<std::uint32_t> mem_vector((uint32_t*)mem_ptr, (uint32_t*)mem_ptr + size / sizeof(uint32_t));
  write_to_device(mem_vector, core, addr, tlb_to_use, send_epoch_cmd, last_send_epoch_cmd, ordered_with_prev_remote_write);
}

void tt_simulation_device::write_to_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {

  std::vector<uint8_t> byte_data(vec.size() * sizeof(uint32_t));
  std::memcpy(byte_data.data(), vec.data(), byte_data.size());

  write(core, addr, byte_data);
}

void tt_simulation_device::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
    // Placeholder - implement later - https://yyz-gitlab.local.tenstorrent.com/tenstorrent/open-umd/-/issues/26
}

void tt_simulation_device::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
    // Placeholder - implement later - https://yyz-gitlab.local.tenstorrent.com/tenstorrent/open-umd/-/issues/26
}

void tt_simulation_device::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {
    // Placeholder - implement later - https://yyz-gitlab.local.tenstorrent.com/tenstorrent/open-umd/-/issues/26
}



void tt_simulation_device::read_from_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& /*tlb_to_use*/) {
  std::vector<uint8_t> byte_data = read(core, addr, size);

  // Verify that the received byte data can be converted to uint32_t
  // if (byte_data.size() % sizeof(uint32_t) != 0) {
  //   throw std::runtime_error("Received byte data size is not a multiple of uint32_t size.");
  // }

  vec.clear();
  vec.resize(byte_data.size() / sizeof(uint32_t));
  std::memcpy(vec.data(), byte_data.data(), byte_data.size());
}

void tt_simulation_device::translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c) {
  // No translation is performed
  return;
}
tt_ClusterDescriptor* tt_simulation_device::get_cluster_description() { return ndesc.get(); }

std::set<chip_id_t> tt_simulation_device::get_target_mmio_device_ids() {
  log_error("LogSimulationDriver: get_target_mmio_device_ids not implemented");
  return {};
}

std::set<chip_id_t> tt_simulation_device::get_target_remote_device_ids() {
  log_error("LogSimulationDriver: get_target_remote_device_ids not implemented");
  return {};
}

void tt_simulation_device::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {
    dram_address_params = dram_address_params_;
}
int tt_simulation_device::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_simulation_device::get_all_chips_in_cluster() { return { 0 }; }
int tt_simulation_device::detect_number_of_chips() { return 1; }

bool tt_simulation_device::using_harvested_soc_descriptors() { return false; }
bool tt_simulation_device::noc_translation_en() { return false; }
std::unordered_map<chip_id_t, uint32_t> tt_simulation_device::get_harvesting_masks_for_soc_descriptors() { return {{0, 0}};}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_simulation_device::get_virtual_soc_descriptors() {return soc_descriptor_per_chip;}

std::map<int, int> tt_simulation_device::get_clocks() {
  return std::map<int, int>();
}

void tt_simulation_device::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
  l1_address_params = l1_address_params_;
}



