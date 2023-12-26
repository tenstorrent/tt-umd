#include <stdexcept>
#include <cstring>

#include "tt_emulation_device.h"
#include "tt_emu_zemi3_wrapper.h"


tt_emulation_device::tt_emulation_device(const std::string& sdesc_path) : tt_device(sdesc_path) {
  soc_descriptor_per_chip.emplace(0, tt_SocDescriptor(sdesc_path));
  
  tt_zebu_wrapper_inst = new tt_emu_zemi3_wrapper();

  std::cout << "Created Emulation Device " << std::endl;
}

tt_emulation_device::~tt_emulation_device() {
  ndesc.reset();
  delete tt_zebu_wrapper_inst;
  std::cout << "Destroyed Emulation Device " << std::endl;
}
  
void tt_emulation_device::write(tt_cxy_pair core, uint64_t addr, const std::vector<uint8_t>& data) {
  const uint32_t size = static_cast<uint32_t>(data.size());
  tt_zebu_wrapper_inst->axi_write(0, core.x, core.y, addr, size, data);
  std::cout << "Wrote " << size << " bytes to address " << std::hex << addr << std::dec << std::endl;  
}

std::vector<uint8_t> tt_emulation_device::read(tt_cxy_pair core, uint64_t addr, uint32_t size) {
  std::vector<uint8_t> data(size);
  tt_zebu_wrapper_inst->axi_read(0, core.x, core.y, addr, size, data);
  std::cout << "Read " << size << " bytes from address " << std::hex << addr << std::dec << std::endl;
  return data;
}


void tt_emulation_device::start_device(const tt_device_params& device_params) {
  tt_zebu_wrapper_inst->zebu_start();
  std::cout << "Started Emulation Device " << std::endl;
}

void tt_emulation_device::deassert_risc_reset(int target_device) {
  tt_zebu_wrapper_inst->all_tensix_reset_deassert();
  std::cout << "Deasserted RISC reset for target device " << target_device << std::endl;
}

void tt_emulation_device::assert_risc_reset(int target_device) {
  tt_zebu_wrapper_inst->all_tensix_reset_assert();
  std::cout << "Asserted RISC reset for target device " << target_device << std::endl;
}

void tt_emulation_device::close_device() {
    std::cout << "Closing Emulation Device " << std::endl;
    tt_zebu_wrapper_inst->zebu_finish();
}

void tt_emulation_device::start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool /*init_device*/, bool /*skip_driver_allocs*/
) {
  std::cout << "Starting Emulation Device " << std::endl;
}


 
void tt_emulation_device::rolled_write_to_device(std::vector<uint32_t>& base_vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t base_addr, const std::string& tlb_to_use) {
  std::vector<uint32_t> vec = base_vec;
  uint32_t byte_increment = 4 * vec.size();
  for (uint32_t i = 0; i < unroll_count; ++i) {
    vec[0] = i; // slot id for debug
    uint64_t offset_addr = base_addr + i * byte_increment;
    write_to_device(vec, core, offset_addr, tlb_to_use);
  }
}


void tt_emulation_device::write_to_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd, bool last_send_epoch_cmd) {
//   DEBUG_LOG("Emulation Device (" << get_sim_time(*versim) << "): Write vector at target core " << target.str() << ", address: " << std::hex << address << std::dec);

  std::vector<uint8_t> byte_data(vec.size() * sizeof(uint32_t));
  std::memcpy(byte_data.data(), vec.data(), byte_data.size());

  write(core, addr, byte_data);
}


void tt_emulation_device::read_from_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& /*tlb_to_use*/) {
//   DEBUG_LOG("Versim Device (" << get_sim_time(*versim) << "): Read vector from target address: 0x" << std::hex << addr << std::dec << ", with size: " << size << " Bytes");
  std::vector<uint8_t> byte_data = read(core, addr, size * sizeof(uint32_t));

  // Verify that the received byte data can be converted to uint32_t
  // if (byte_data.size() % sizeof(uint32_t) != 0) {
  //   throw std::runtime_error("Received byte data size is not a multiple of uint32_t size.");
  // }

  vec.clear();
  vec.resize(byte_data.size() / sizeof(uint32_t));
  std::memcpy(vec.data(), byte_data.data(), byte_data.size());
}


// -------------------------
// Not sure how to implement these functions below, leaving them blank/default for now
void tt_emulation_device::translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c) {
  // No translation is performed
  return;
}
tt_ClusterDescriptor* tt_emulation_device::get_cluster_description() { return ndesc.get(); }

std::set<chip_id_t> tt_emulation_device::get_target_mmio_device_ids() {
  std::cerr << "get_target_mmio_device_ids not implemented" << std::endl;
  return {};
}

std::set<chip_id_t> tt_emulation_device::get_target_remote_device_ids() {
  std::cerr << "get_target_remote_device_ids not implemented" << std::endl;
  return {};
}


int tt_emulation_device::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_emulation_device::get_all_chips_in_cluster() { return { 0 }; }
int tt_emulation_device::detect_number_of_chips() { return 1; }

bool tt_emulation_device::using_harvested_soc_descriptors() { return false; }
bool tt_emulation_device::noc_translation_en() { return false; }
std::unordered_map<chip_id_t, uint32_t> tt_emulation_device::get_harvesting_masks_for_soc_descriptors() { return {{0, 0}};}

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_emulation_device::get_virtual_soc_descriptors() {return soc_descriptor_per_chip;}

std::map<int, int> tt_emulation_device::get_clocks() {
  return std::map<int, int>();
}

void tt_emulation_device::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
  l1_address_params = l1_address_params_;
}



