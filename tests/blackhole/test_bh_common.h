#pragma once
#include "../test_utils/stimulus_generators.hpp"
#include "eth_l1_address_map.h"
#include "tt_xy_pair.h"
#include <tt_cluster_descriptor.h>
#include <tt_device.h>

namespace tt::umd::test::utils {

static void set_params_for_remote_txn(tt_SiliconDevice& device) {
    // Populate address map and NOC parameters that the driver needs for remote transactions
    device.set_driver_host_address_params({host_mem::address_map::ETH_ROUTING_BLOCK_SIZE, host_mem::address_map::ETH_ROUTING_BUFFERS_START});

    device.set_driver_eth_interface_params({NOC_ADDR_LOCAL_BITS, NOC_ADDR_NODE_ID_BITS, ETH_RACK_COORD_WIDTH, CMD_BUF_SIZE_MASK, MAX_BLOCK_SIZE,
                                            REQUEST_CMD_QUEUE_BASE, RESPONSE_CMD_QUEUE_BASE, CMD_COUNTERS_SIZE_BYTES, REMOTE_UPDATE_PTR_SIZE_BYTES,
                                            CMD_DATA_BLOCK, CMD_WR_REQ, CMD_WR_ACK, CMD_RD_REQ, CMD_RD_DATA, CMD_BUF_SIZE, CMD_DATA_BLOCK_DRAM, ETH_ROUTING_DATA_BUFFER_ADDR,
                                            REQUEST_ROUTING_CMD_QUEUE_BASE, RESPONSE_ROUTING_CMD_QUEUE_BASE, CMD_BUF_PTR_MASK, CMD_ORDERED, CMD_BROADCAST});
    
    device.set_device_l1_address_params({l1_mem::address_map::NCRISC_FIRMWARE_BASE, l1_mem::address_map::FIRMWARE_BASE,
                                  l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE,
                                  l1_mem::address_map::TRISC_BASE, l1_mem::address_map::L1_BARRIER_BASE, eth_l1_mem::address_map::ERISC_BARRIER_BASE, eth_l1_mem::address_map::FW_VERSION_ADDR});
}

class BlackholeTestFixture : public ::testing::Test {
 protected:
  // You can remove any or all of the following functions if their bodies would
  // be empty.

  std::unique_ptr<tt_SiliconDevice> device;

  BlackholeTestFixture() {

  }

  ~BlackholeTestFixture() override {
     // You can do clean-up work that doesn't throw exceptions here.
  }

  virtual int get_detected_num_chips() = 0;
  virtual bool is_test_skipped() = 0;

  // If the constructor and destructor are not enough for setting up
  // and cleaning up each test, you can define the following methods:

  void SetUp() override {
    // Code here will be called immediately after the constructor (right
    // before each test).

    if (is_test_skipped()) {
        GTEST_SKIP() << "Test is skipped due to incorrect number of chips";
    }

    // std::cout << "Setting Up Test." << std::endl;
    assert(get_detected_num_chips() > 0);
    auto devices = std::vector<chip_id_t>(get_detected_num_chips());
    std::iota(devices.begin(), devices.end(), 0);
    std::set<chip_id_t> target_devices = {devices.begin(), devices.end()};
    uint32_t num_host_mem_ch_per_mmio_device = 1;
    std::unordered_map<std::string, std::int32_t> dynamic_tlb_config = {}; // Don't set any dynamic TLBs in this test
    device = std::make_unique<tt_SiliconDevice>(SOC_DESC_PATH, GetClusterDescYAML().string(), target_devices, num_host_mem_ch_per_mmio_device, dynamic_tlb_config, false, true, true);
    assert(device != nullptr);
    assert(device->get_cluster_description()->get_number_of_chips() == get_detected_num_chips());

    set_params_for_remote_txn(*device);

    tt_device_params default_params;
    device->start_device(default_params);

    device->deassert_risc_reset();

    device->wait_for_non_mmio_flush();
  }

  void TearDown() override {
    // Code here will be called immediately after each test (right
    // before the destructor).

    if (!is_test_skipped()) {
        // std::cout << "Tearing Down Test." << std::endl;
        device->close_device();
    }
  }

};

} // namespace tt::umd::test::utils