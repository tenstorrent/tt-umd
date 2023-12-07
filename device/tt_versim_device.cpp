// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0



#include "tt_device.h"
#include "device/driver_atomics.h"
#include "common/logger.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

// TODO: Remove dependency on command_assembler + soc
#include "command_assembler/soc.h"
#include "device/tt_cluster_descriptor.h"
namespace CA = CommandAssembler;


void translate_soc_descriptor_to_ca_soc(CA::Soc &soc, const tt_SocDescriptor soc_descriptor) {
  for (auto &core : soc_descriptor.cores) {
    CA::SocNocNode node;
    CA::xy_pair CA_coord(core.first.x, core.first.y);
    node.noc_coord = CA_coord;
    node.memory_size = core.second.l1_size;
    switch (core.second.type) {
      case CoreType::ARC: node.arc = true; break;
      case CoreType::DRAM: {
        node.dram = true; 
        #ifdef EN_DRAM_ALIAS
          node.dram_channel_id = std::get<0>(soc_descriptor.dram_core_channel_map.at(core.first));
        #endif
      } break;
      case CoreType::ETH: node.eth = true; break;
      case CoreType::PCIE: node.pcie = true; break;
      case CoreType::WORKER: node.worker = true; break;
      case CoreType::HARVESTED: node.harvested = true; break;
      case CoreType::ROUTER_ONLY: node.router_only = true; break;
      default: std::cout << " Error: Unsupported CoreType type: " << static_cast<int>(core.second.type) << std::endl; break;
    }
    soc.SetNodeProperties(node.noc_coord, node);
  }
}

////////
// Device Versim
////////

#include "device.h"
#include "sim_interactive.h"
#include <command_assembler/xy_pair.h>

tt_VersimDevice::tt_VersimDevice(const std::string &sdesc_path, const std::string &ndesc_path) : tt_device(sdesc_path) {
  soc_descriptor_per_chip.emplace(0, tt_SocDescriptor(sdesc_path));
  std::set<chip_id_t> target_devices = {0};
  if (ndesc_path == "") {
    ndesc = tt_ClusterDescriptor::create_for_grayskull_cluster(target_devices, {});
  } 
  else {
    ndesc = tt_ClusterDescriptor::create_from_yaml(ndesc_path);
  }
}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_VersimDevice::get_virtual_soc_descriptors() {return soc_descriptor_per_chip;}

tt_ClusterDescriptor* tt_VersimDevice::get_cluster_description() {return ndesc.get();}
void tt_VersimDevice::start_device(const tt_device_params &device_params) {
  bool no_checkers = true;
  std::vector<std::string> dump_cores = device_params.unroll_vcd_dump_cores(get_soc_descriptor(0) -> grid_size);
  start(device_params.expand_plusargs(), dump_cores, no_checkers, device_params.init_device, false);
}

void tt_VersimDevice::close_device() {
  stop();
}

void tt_VersimDevice::start(
    std::vector<std::string> plusargs,
    std::vector<std::string> dump_cores,
    bool no_checkers,
    bool /*init_device*/,
    bool /*skip_driver_allocs*/
    ) {

     std::cout << "Start Versim Device " << std::endl;
     std::string device_descriptor_dir = "./";

     std::optional<std::string> vcd_suffix;
     if (dump_cores.size() > 0) {
       vcd_suffix = "core_dump.vcd";
     }

     std::vector<std::string> vcd_cores;

     // TODO: For now create a temporary stuff from CA and populate from descriptor before passing back to versim-core
     // interface. mainly bypasses arch_configs etc from llir.  We can populate soc directly
     // MT: have to preserve ca_soc_descriptor object since versim references it at runtime
     CA::xy_pair CA_grid_size((soc_descriptor_per_chip.begin() -> second).grid_size.x, (soc_descriptor_per_chip.begin() -> second).grid_size.y);
     // CA::Soc ca_soc_manager(CA_grid_size);
     std::unique_ptr<CA::Soc> p_ca_soc_manager_unique = std::make_unique<CA::Soc>(CA_grid_size);
     translate_soc_descriptor_to_ca_soc(*p_ca_soc_manager_unique, (soc_descriptor_per_chip.begin() -> second));
     // TODO: End

     std::cout << "Versim Device: turn_on_device ";
     std::vector<std::uint32_t> trisc_sizes = {static_cast<unsigned int>(l1_address_params.TRISC0_SIZE), static_cast<unsigned int>(l1_address_params.TRISC1_SIZE), static_cast<unsigned int>(l1_address_params.TRISC2_SIZE)};
     std::unique_ptr<versim::VersimSimulator> versim_unique = versim::turn_on_device(CA_grid_size, *p_ca_soc_manager_unique, plusargs, vcd_suffix, dump_cores, no_checkers,
        l1_address_params.TRISC_BASE, trisc_sizes);
     versim = versim_unique.release();

     std::cout << "Versim Device: write info to tvm db " << std::endl;
     versim::write_info_to_tvm_db(l1_address_params.TRISC_BASE, trisc_sizes);
     versim::build_and_connect_tvm_phase();

     versim->spin_threads(*p_ca_soc_manager_unique, false);
     versim::assert_reset(*versim);

     p_ca_soc_manager = (void*)(p_ca_soc_manager_unique.release());

     std::cout << "Versim Device: Done start " << std::endl;
}

tt_VersimDevice::~tt_VersimDevice () {
  ndesc.reset();
}

// bool tt_VersimDevice::run() {
//   std::cout << "Versim Device: Run " << std::endl;

//   // Run Versim main_loop
//   versim::startup_versim_main_loop(*versim);

//   return true;
// }

void tt_VersimDevice::deassert_risc_reset() {
  std::cout << "Versim Device: Deassert risc resets start" << std::endl;
  versim::handle_resetting_triscs(*versim);
  std::cout << "Versim Device: Start main loop " << std::endl;
  versim::startup_versim_main_loop(*versim);
}

void tt_VersimDevice::deassert_risc_reset_at_core(tt_cxy_pair core) {
  // This function deasserts reset on the full versim device (don't need core level granularity for versim)
 deassert_risc_reset();
}

void tt_VersimDevice::assert_risc_reset() {
  std::cout << "Pause all the cores" << std::endl;
  versim::pause(*versim);

  std::cout << "Wait for cores to go to paused state" << std::endl;
  versim::sleep_wait_for_paused (*versim);

  std::cout << "Assert riscv reset" << std::endl;
  versim::assert_riscv_reset(*versim);
}

void tt_VersimDevice::assert_risc_reset_at_core(tt_cxy_pair core) {
  // This function asserts reset on the full versim device (don't need core level granularity for versim)
 assert_risc_reset();
}

void tt_VersimDevice::rolled_write_to_device(std::vector<uint32_t> &vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use) {
  uint32_t byte_increment = vec.size() * 4; 
  for (int i=0; i<unroll_count; i++) {
      vec[0] = i; // slot id for debug
      write_to_device(vec, core, addr + i * byte_increment, tlb_to_use);
  }
}

void tt_VersimDevice::rolled_write_to_device(uint32_t* mem_ptr, uint32_t len, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& fallback_tlb) {
  std::vector<std::uint32_t> mem_vector(mem_ptr, mem_ptr + len);
  rolled_write_to_device(mem_vector, unroll_count, core, addr, fallback_tlb);
}

void tt_VersimDevice::write_to_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
  
  log_debug(tt::LogSiliconDriver, "Versim Device ({}): Write vector at target core {}, address: {}", get_sim_time(*versim), core.str(), addr);

  bool aligned_32B = (soc_descriptor_per_chip.begin() -> second).cores.at(core).type == CoreType::DRAM;
  // MT: Remove these completely
  CommandAssembler::xy_pair CA_target(core.x, core.y);
  CommandAssembler::memory CA_tensor_memory(addr, vec);

  nuapi::device::write_memory_to_core(*versim, CA_target, CA_tensor_memory);
}

void tt_VersimDevice::write_to_device(const void *mem_ptr, uint32_t size, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd, bool ordered_with_prev_remote_write) {
  log_assert(!(size % 4), "Writes to Versim Backend should be 4 byte aligned!");

  std::vector<std::uint32_t> mem_vector((uint32_t*)mem_ptr, (uint32_t*)mem_ptr + size / sizeof(uint32_t));
  write_to_device(mem_vector, core, addr, tlb_to_use, send_epoch_cmd, last_send_epoch_cmd, ordered_with_prev_remote_write);
}

void tt_VersimDevice::broadcast_write_to_cluster(const void *mem_ptr, uint32_t size_in_bytes, uint64_t address, const std::set<chip_id_t>& chips_to_exclude, std::set<uint32_t>& rows_to_exclude, std::set<uint32_t>& cols_to_exclude, const std::string& fallback_tlb) {
  for(const auto& core : get_soc_descriptor(0) -> cores) {
    if(cols_to_exclude.find(core.first.x) == cols_to_exclude.end() and rows_to_exclude.find(core.first.y) == rows_to_exclude.end() and core.second.type != CoreType::HARVESTED) {
        write_to_device(mem_ptr, size_in_bytes, tt_cxy_pair(0, core.first.x, core.first.y), address, "");
      }
  }
}
void tt_VersimDevice::wait_for_non_mmio_flush() {
  // Do nothing, since Versim does not simulate non-mmio mapped chips
}

void tt_VersimDevice::l1_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& cores) {
  tt_driver_atomics::mfence(); // Ensure no reordering of loads/stores around this
}

void tt_VersimDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<uint32_t>& channels) {
  tt_driver_atomics::mfence(); // Ensure no reordering of loads/stores around this
}

void tt_VersimDevice::dram_membar(const chip_id_t chip, const std::string& fallback_tlb, const std::unordered_set<tt_xy_pair>& dram_cores) {
  tt_driver_atomics::mfence(); // Ensure no reordering of loads/stores around this
}

void tt_VersimDevice::read_from_device(std::vector<uint32_t> &vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {
  log_debug(tt::LogSiliconDriver, "Versim Device ({}): Read vector from address: {}, with size: {} Bytes", get_sim_time(*versim), addr, size);

  CommandAssembler::xy_pair CA_target(core.x, core.y);

  size_t size_in_words = size / 4;
  auto result = nuapi::device::read_memory_from_core(*versim, CA_target, addr, size_in_words);
  vec = result;
}

void tt_VersimDevice::read_from_device(void *mem_ptr, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use) {
  log_debug(tt::LogSiliconDriver, "Versim Device ({}): Read vector from address: {}, with size: {} Bytes", get_sim_time(*versim), addr, size);
  log_assert(!(size % 4), "Reads from Versim backend should be 4 byte aligned!");

  CommandAssembler::xy_pair CA_target(core.x, core.y);

  size_t size_in_words = size / 4;
  auto result = nuapi::device::read_memory_from_core(*versim, CA_target, addr, size_in_words);
  memcpy(mem_ptr, result.data(), result.size()*sizeof(uint32_t));
}

void tt_VersimDevice::translate_to_noc_table_coords(chip_id_t device_id, std::size_t &r, std::size_t &c) {
  // No translation is performed
  return;
}

std::set<chip_id_t> tt_VersimDevice::get_target_mmio_device_ids() {
  // Must only be used for silicon
  return {};
}

std::set<chip_id_t> tt_VersimDevice::get_target_remote_device_ids() {
  // Must only be used for silicon
  return {};
}


bool versim_check_dram_core_exists(const std::vector<std::vector<tt_xy_pair>> &dram_core_channels, tt_xy_pair target_core) {
    bool dram_core_exists = false;
    for (const auto &dram_cores_in_channel: dram_core_channels) {
      for (const auto &dram_core : dram_cores_in_channel) {
        if (dram_core.x == target_core.x && dram_core.y == target_core.y) {
            return true;
        }
      }
    }
    return false;
}

int tt_VersimDevice::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_VersimDevice::get_all_chips_in_cluster() { return {0}; }
int tt_VersimDevice::detect_number_of_chips() { return 1; }

bool tt_VersimDevice::using_harvested_soc_descriptors() { return false; }
bool tt_VersimDevice::noc_translation_en() { return false; }
std::unordered_map<chip_id_t, uint32_t> tt_VersimDevice::get_harvesting_masks_for_soc_descriptors() { return {{0, 0}};}

// Meant to breakout running functions for simulator
bool tt_VersimDevice::stop() {
  std::cout << "Versim Device: Stop " << std::endl;

  versim::turn_off_device(*versim);
  versim->shutdown();
  // Force free of all versim cores
  for (auto x = 0; x < versim->grid_size.x; x++) {
    for (auto y = 0; y < versim->grid_size.y; y++) {
      delete versim->core_grid.at(x).at(y);
    }
  }
  std::cout << "Versim Device: Stop completed " << std::endl;
  delete versim;
  return true;
}

std::map<int,int> tt_VersimDevice::get_clocks() {
  return std::map<int,int>();
}

void tt_VersimDevice::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
    l1_address_params = l1_address_params_;
}

void tt_VersimDevice::set_device_dram_address_params(const tt_device_dram_address_params& dram_address_params_) {
    dram_address_params = dram_address_params_;
}

std::uint32_t tt_VersimDevice::get_num_dram_channels(std::uint32_t device_id) {
    return get_soc_descriptor(device_id) -> get_num_dram_channels();
}

std::uint32_t tt_VersimDevice::get_dram_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    return get_soc_descriptor(device_id) -> dram_bank_size; // Space per channel is identical for now
}

std::uint32_t tt_VersimDevice::get_num_host_channels(std::uint32_t device_id) {
    // Host buffers not allocated for Versim Devices
    return 0;
}

std::uint32_t tt_VersimDevice::get_host_channel_size(std::uint32_t device_id, std::uint32_t channel) {
    // Host buffers not allocated for Versim Devices
    return 0;
}