#pragma once

#include <cstdint>
#include <fstream>
#include <vector>
#include "tt_soc_descriptor.h"
#include "tt_xy_pair.h"


class tt_rtl_device : public tt_device {
public:
  virtual void set_device_l1_address_params(const tt_device_l1_address_params& l1_address_params_); // Dont care
  tt_rtl_device(const std::string& request_path, const std::string& response_path);
  virtual void start(std::vector<std::string> plusargs, std::vector<std::string> dump_cores, bool no_checkers, bool init_device, bool skip_driver_allocs);
  virtual void start_device(const tt_device_params& device_params);
  virtual void close_device();
  virtual void deassert_risc_reset(int target_device);
  virtual void assert_risc_reset(int target_device);
  virtual void write_to_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use, bool send_epoch_cmd = false, bool last_send_epoch_cmd = true);
  virtual void rolled_write_to_device(std::vector<uint32_t>& vec, uint32_t unroll_count, tt_cxy_pair core, uint64_t addr, const std::string& tlb_to_use); // See Versim Implementation
  virtual void read_from_device(std::vector<uint32_t>& vec, tt_cxy_pair core, uint64_t addr, uint32_t size, const std::string& tlb_to_use);

  virtual void translate_to_noc_table_coords(chip_id_t device_id, std::size_t& r, std::size_t& c); // return
  virtual std::set<chip_id_t> get_target_mmio_device_ids(); // return  {}
  virtual std::set<chip_id_t> get_target_remote_device_ids(); // return {}
  virtual ~tt_rtl_device(); // Should close the FIFOs
  virtual tt_ClusterDescriptor* get_cluster_description(); // Return "info" from vcs compile.  Need to implement this
  virtual int get_number_of_chips_in_cluster(); // Return detect_number_of_chips()
  virtual std::unordered_set<chip_id_t> get_all_chips_in_cluster(); // Return {0}
  static int detect_number_of_chips();  // Return 1
  virtual std::map<int, int> get_clocks(); // Not Implemented
private:

  tt_device_l1_address_params l1_address_params;
  std::shared_ptr<tt_ClusterDescriptor> ndesc;

  bool stop();
  std::ofstream request_fifo;
  std::ifstream response_fifo;
  uint32_t transaction_id = 0;
  uint8_t risc_resetn = 0;

  // These functions implement the "protocol" between the RTL simulation and the UMD
  void write(tt_cxy_pair core, uint64_t addr, uint32_t size, const std::vector<uint8_t>& data);
  std::vector<uint8_t> read(tt_cxy_pair core, uint64_t addr, uint32_t size);
  void exit();
  void set_risc_reset(uint8_t reset_mask);
  void send_command(char command, tt_cxy_pair core, uint64_t addr, uint32_t size, uint32_t id, const std::vector<uint8_t>& data);
  bool receive_response(char expected_command, uint32_t expected_id);
  std::vector<uint8_t> receive_data_response(char expected_command, uint32_t expected_id, uint32_t size);

  
};

