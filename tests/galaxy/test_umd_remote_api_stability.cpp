#include <tt_cluster_descriptor.h>
#include <tt_device.h>

#include <cstdint>
#include <numeric>
#include <random>
#include <util.hpp>
#include <thread>

#include "common/logger.hpp"
#include "device_data.hpp"
#include "eth_interface.h"
#include "filesystem"
#include "gtest/gtest.h"
#include "host_mem_address_map.h"
#include "l1_address_map.h"
#include "../galaxy/test_galaxy_common.h"
#include "tt_soc_descriptor.h"

#include "../test_utils/stimulus_generators.hpp"
#include "../wormhole/test_wh_common.h"


namespace tt::umd::test::utils {


class WormholeGalaxyStabilityTestFixture : public WormholeTestFixture {
 private:
  static int detected_num_chips;
  static bool skip_tests;

 protected:

  static constexpr int EXPECTED_MIN_CHIPS = 32;

  static void SetUpTestSuite() {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(GetClusterDescYAML().string());
    detected_num_chips = cluster_desc->get_number_of_chips();
    if (detected_num_chips < EXPECTED_MIN_CHIPS) {
        skip_tests = true;
    }
  }

  virtual int get_detected_num_chips() {
    return detected_num_chips;
  }

  virtual bool is_test_skipped() {
    return skip_tests;
  }

};


int WormholeGalaxyStabilityTestFixture::detected_num_chips = -1;
bool WormholeGalaxyStabilityTestFixture::skip_tests = false;


TEST_F(WormholeGalaxyStabilityTestFixture, MixedRemoteTransfers) {
    int seed = 0;
    
    assert(device != nullptr);
    std::cout << "Running Test." << std::endl;
    std::vector<remote_transfer_sample_t> command_history;
    try {
        RunMixedTransfersUniformDistributions(
            *this->device, 
            100000,
            seed,

            transfer_type_weights_t{.write = 0.40, .rolled_write = 0.2, .read = 0.4, .epoch_cmd_write = 0.0},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history
        );
    } catch (...) {
        print_command_history_executable_code(command_history);
    }

}

TEST_F(WormholeGalaxyStabilityTestFixture, DISABLED_MultithreadedMixedRemoteTransfersMediumSmall) {
    int seed = 0;

    std::cout << "Running commands in multithreaded mode." << std::endl;
    assert(device != nullptr);
    std::thread t1([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            50000,
            0,

            transfer_type_weights_t{.write = 0.50, .rolled_write = 0., .read = 0.50, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            nullptr
        );    
    });
    std::thread t2([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            50000,
            100,

            transfer_type_weights_t{.write = 0.25, .rolled_write = 0.25, .read = 0.50, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            nullptr
        );    
    });
    std::thread t3([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            50000,
            23,

            transfer_type_weights_t{.write = 0.5, .rolled_write = 0.25, .read = 0.25, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            nullptr
        );    
    });
    std::thread t4([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000,
            99,

            transfer_type_weights_t{.write = 0.1, .rolled_write = 0, .read = 0.1, .epoch_cmd_write = 0.8},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            nullptr
        );    
    });

    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

} // namespace tt::umd::test::utils