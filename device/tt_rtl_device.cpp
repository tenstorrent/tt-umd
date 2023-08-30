#include "tt_rtl_device.hpp"
#include <stdexcept>
#include <cstring>


struct coord_t {
  uint8_t x;
  uint8_t y;

  // create coord_t from tt_cxy_pair
  explicit coord_t(const tt_cxy_pair& pair)
    : x(static_cast<uint8_t>(pair.x & 0xff)),
    y(static_cast<uint8_t>(pair.y & 0xff)) {}
};

std::unordered_map<chip_id_t, tt_SocDescriptor>& tt_rtl_device::get_virtual_soc_descriptors() { return soc_descriptor_per_chip; }

tt_ClusterDescriptor* tt_rtl_device::get_cluster_description() { return ndesc.get(); }
void tt_rtl_device::start_device(const tt_device_params& device_params) {
  bool no_checkers = true;

  // TODO: What is this actually returning?

  // Versim Code:
  // std::vector<std::string> dump_cores = device_params.unroll_vcd_dump_cores(get_soc_descriptor(0) -> grid_size);
  // start(device_params.expand_plusargs(), dump_cores, no_checkers, device_params.init_device, device_params.skip_driver_allocs);
}

void tt_rtl_device::close_device() {
  exit();
}

void tt_rtl_device::start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool /*init_device*/, bool /*skip_driver_allocs*/
) {
  std::cout << "Starting Simulation Device " << std::endl;
}

void tt_rtl_device::deassert_risc_reset(int target_device) {
  reset_mask |= 1 << target_device;
  set_risc_reset(reset_mask)
}

void tt_rtl_device::assert_risc_reset(int target_device) {
  reset_mask &= ~(1 << target_device);
  set_risc_reset(reset_mask)
}

void tt_rtl_device::rolled_write_to_device(const std::vector<uint32_t>& base_vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t base_addr, const std::string& tlb_to_use) {
  std::vector<uint32_t> vec = base_vec;
  uint32_t byte_increment = 4 * vec.size();
  for (uint32_t i = 0; i < unroll_count; ++i) {
    vec[0] = i; // slot id for debug
    uint64_t offset_addr = base_addr + i * byte_increment;
    write_to_device(vec, core, offset_addr, tlb_to_use);
  }
}


void tt_rtl_device::write_to_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, const std::string& /*tlb_to_use*/, bool /*send_epoch_cmd*/, bool /*last_send_epoch_cmd*/) {
  DEBUG_LOG("Simulation Device (" << get_sim_time(*versim) << "): Write vector at target core " << target.str() << ", address: " << std::hex << address << std::dec);

  std::vector<uint8_t> byte_data(vec.size() * sizeof(uint32_t));
  std::memcpy(byte_data.data(), vec.data(), byte_data.size());

  write(core, addr, byte_data.size(), byte_data);
}


void tt_rtl_device::read_from_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& /*tlb_to_use*/) {
  DEBUG_LOG("Versim Device (" << get_sim_time(*versim) << "): Read vector from target address: 0x" << std::hex << addr << std::dec << ", with size: " << size << " Bytes");
  std::vector<uint8_t> byte_data = read(core, addr, size);

  // Verify that the received byte data can be converted to uint32_t
  // if (byte_data.size() % sizeof(uint32_t) != 0) {
  //   throw std::runtime_error("Received byte data size is not a multiple of uint32_t size.");
  // }

  vec.clear();
  vec.resize(byte_data.size() / sizeof(uint32_t));
  std::memcpy(vec.data(), byte_data.data(), byte_data.size());
}

void tt_rtl_device::translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c) {
  std::cerr << "translate_to_noc_table_coords not implemented" << std::endl;
  return;
}

std::set<chip_id_t> tt_rtl_device::get_target_mmio_device_ids() {
  std::cerr << "get_target_mmio_device_ids not implemented" << std::endl;
  return {};
}

std::set<chip_id_t> tt_rtl_device::get_target_remote_device_ids() {
  std::cerr << "get_target_remote_device_ids not implemented" << std::endl;
  return {};
}


int tt_rtl_device::get_number_of_chips_in_cluster() { return detect_number_of_chips(); }
std::unordered_set<int> tt_rtl_device::get_all_chips_in_cluster() { return { 0 }; }
int tt_rtl_device::detect_number_of_chips() { return 1; }

bool tt_rtl_device::using_harvested_soc_descriptors() { return false; }
bool tt_rtl_device::noc_translation_en() { return false; }
std::unordered_map<chip_id_t, uint32_t> tt_rtl_device::get_harvesting_masks_for_soc_descriptors() { return { {0, 0} }; }

bool tt_rtl_device::stop() {
  std::cout << "Stopping Simulation Device " << std::endl;
  exit();

  if (request_fifo.is_open()) {
    request_fifo.close();
  }
  if (response_fifo.is_open()) {
    response_fifo.close();
  }

  std::cout << "Simulation Device: Stop completed " << std::endl;
  return true;
}

std::map<int, int> tt_rtl_device::get_clocks() {
  return std::map<int, int>();
}

void tt_rtl_device::set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_) {
  l1_address_params = l1_address_params_;
}

tt_rtl_device::tt_rtl_device(const std::string& request_path, const std::string& response_path)
  : request_fifo(request_path, std::ios::binary | std::ios::out),
  response_fifo(response_path, std::ios::binary | std::ios::in) {
  if (!request_fifo.is_open() || !response_fifo.is_open()) {
    throw std::runtime_error("Failed to open FIFOs.");
  }
}

tt_rtl_device::~tt_rtl_device() {
  ndesc.reset();
  if (request_fifo.is_open()) {
    request_fifo.close();
  }
  if (response_fifo.is_open()) {
    response_fifo.close();
  }
}

void tt_rtl_device::write(tt_cxy_pair core, uint64_t addr, const std::vector<uint8_t>& data) {
  const uint32_t size = static_cast<uint32_t>(data.size());
  send_command('w', core, addr, size, transaction_id, data);
  bool success = receive_response('w', transaction_id);
  if (!success) {
    throw std::runtime_error("Write operation failed.");
  }
  transaction_id++;
}

std::vector<uint8_t> tt_rtl_device::read(tt_cxy_pair core, uint64_t addr, uint32_t size) {
  send_command('r', core, addr, size, transaction_id, {});
  auto data = receive_data_response('r', transaction_id, size);
  transaction_id++;
  return data;
}

void tt_rtl_device::exit() {
  char command = 'e';
  uint32_t exit_id = transaction_id;
  request_fifo.write(&command, 1);
  request_fifo.write(reinterpret_cast<const char*>(&exit_id), sizeof(exit_id));
  request_fifo.flush();

  bool success = receive_response('e', exit_id);
  if (!success) {
    throw std::runtime_error("Exit command failed.");
  }
}

void tt_rtl_device::set_risc_reset(uint8_t reset_mask) {
  char command = 'r';
  uint32_t assert_id = transaction_id++;
  // Write command and transaction ID to FIFO
  request_fifo.write(&command, 1);
  request_fifo.write(reinterpret_cast<const char*>(&assert_id), sizeof(assert_id));
  // Write reset_mask to FIFO
  request_fifo.write(reinterpret_cast<const char*>(&reset_mask), sizeof(reset_mask));
  request_fifo.flush();

  bool success = receive_response('r', assert_id);
  if (!success) {
    throw std::runtime_error("Assert reset command failed.");
  }
}


void tt_rtl_device::send_command(char command, tt_cxy_pair core, uint64_t addr, uint32_t size, uint32_t id, const std::vector<uint8_t>& data) {
  // We extract the x and y coordinates from the tt_cxy_pair and send them as a coord_t
  const coord_t xy_coord = coord_t(core);
  request_fifo.write(&command, 1);
  request_fifo.write(reinterpret_cast<char*>(&xy_coord), sizeof(xy_coord));
  request_fifo.write(reinterpret_cast<char*>(&addr), sizeof(addr));
  request_fifo.write(reinterpret_cast<char*>(&size), sizeof(size));
  request_fifo.write(reinterpret_cast<char*>(&id), sizeof(id));
  if (!data.empty()) {
    request_fifo.write(reinterpret_cast<const char*>(data.data()), data.size());
  }
  request_fifo.flush();
}

bool tt_rtl_device::receive_response(char expected_command, uint32_t expected_id) {
  char response_command;
  uint32_t response_id;
  response_fifo.read(&response_command, 1);
  response_fifo.read(reinterpret_cast<char*>(&response_id), sizeof(response_id));
  return response_command == expected_command && response_id == expected_id;
}

std::vector<uint8_t> tt_rtl_device::receive_data_response(char expected_command, uint32_t expected_id, uint32_t size) {
  std::vector<uint8_t> data(size);
  char response_command;
  uint32_t response_id;
  response_fifo.read(&response_command, 1);
  response_fifo.read(reinterpret_cast<char*>(&response_id), sizeof(response_id));
  response_fifo.read(reinterpret_cast<char*>(data.data()), size);
  if (response_command != expected_command || response_id != expected_id) {
    throw std::runtime_error("Response mismatch.");
  }
  return data;
}
