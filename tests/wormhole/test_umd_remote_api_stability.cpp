#include <tt_cluster_descriptor.h>
#include <tt_device.h>

#include <cstdint>
#include <numeric>
#include <random>
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
#include "../test_utils/generate_cluster_desc.hpp"
#include "test_wh_common.h"

#include <chrono>
#include <ctime>

namespace tt::umd::test::utils {
class WormholeNebulaX2TestFixture : public WormholeTestFixture {
 private:
  static int detected_num_chips;
  static bool skip_tests;

 protected: 

  static constexpr int EXPECTED_NUM_CHIPS = 2;
  static uint32_t scale_number_of_tests;

  static void SetUpTestSuite() {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(GetClusterDescYAML().string());
    detected_num_chips = cluster_desc->get_number_of_chips();
    if (detected_num_chips != EXPECTED_NUM_CHIPS) {
        skip_tests = true;
    }
    if(char const* scale_number_of_tests_env = std::getenv("SCALE_NUMBER_OF_TESTS")) {
        scale_number_of_tests = std::atoi(scale_number_of_tests_env);
    }
  }

  virtual int get_detected_num_chips() {
    return detected_num_chips;
  }

  virtual bool is_test_skipped() {
    return skip_tests;
  }
};

int WormholeNebulaX2TestFixture::detected_num_chips = -1;
bool WormholeNebulaX2TestFixture::skip_tests = false;
uint32_t WormholeNebulaX2TestFixture::scale_number_of_tests = 1;

TEST_F(WormholeNebulaX2TestFixture, MixedRemoteTransfersMediumSmall) {
    int seed = 0;

    log_info(LogSiliconDriver,"Started MixedRemoteTransfersMediumSmall");

    std::vector<remote_transfer_sample_t> command_history;
    try {
        assert(device != nullptr);
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 0.25, .rolled_write = 0.25, .read = 0.25, .epoch_cmd_write = 0.25},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history
        );
    } catch (...) {
        print_command_history_executable_code(command_history);
    }
}

TEST_F(WormholeNebulaX2TestFixture, MultithreadedMixedRemoteTransfersMediumSmall) {
    int seed = 0;

    log_info(LogSiliconDriver,"Started MultithreadedMixedRemoteTransfersMediumSmall");

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;
    std::vector<remote_transfer_sample_t> command_history2;
    std::vector<remote_transfer_sample_t> command_history3;
    std::thread t1([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 0.50, .rolled_write = 0., .read = 0.50, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        );    
    });
    std::thread t2([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            100,

            transfer_type_weights_t{.write = 0.25, .rolled_write = 0.25, .read = 0.50, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history1
        );    
    });
    std::thread t3([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            23,

            transfer_type_weights_t{.write = 0.5, .rolled_write = 0.25, .read = 0.25, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history2
        );    
    });
    std::thread t4([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            99,

            transfer_type_weights_t{.write = 1.0, .rolled_write = 0, .read = 0.0, .epoch_cmd_write = 0.0},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history3
        );    
    });

    t1.join();
    t2.join();
    t3.join();
    t4.join();
}


TEST_F(WormholeNebulaX2TestFixture, MixedRemoteTransfersLarge) {
    int seed = 0;

    log_info(LogSiliconDriver,"Started MixedRemoteTransfersLarge");

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history;
    try {
        RunMixedTransfersUniformDistributions(
            *device, 
            10000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 0.15, .rolled_write = 0, .read = 0.15, .epoch_cmd_write = 0.7},

            std::uniform_int_distribution<address_t>(0x10000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 300000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 300000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 300000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history
        );
    } catch (...) {
        print_command_history_executable_code(command_history);
    }

}

TEST_F(WormholeNebulaX2TestFixture, WritesOnlyNormalDistributionMean10kStd3kMinSizeTruncate4) {
    int seed = 0;

    log_info(LogSiliconDriver,"Started WritesOnlyNormalDistributionMean10kStd3kMinSizeTruncate4");

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history;

    auto write_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, double, std::normal_distribution>(
        seed, std::normal_distribution<>(10000, 3000), [](double x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });
    

    auto dest_generator = get_default_full_dram_dest_generator(seed, device.get());
    auto address_generator = get_default_address_generator(seed, 0x100000, 0x5000000);

    try {
        RunMixedTransfers(
            *device, 
            10000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 1., .rolled_write = 0., .read = 0., .epoch_cmd_write = 0.},

            WriteCommandGenerator(dest_generator, address_generator, write_size_generator),
            build_dummy_rolled_write_command_generator(*device),
            build_dummy_write_epoch_cmd_command_generator(*device),
            build_dummy_read_command_generator(*device),

            false, // Set to true if you want to emit the command history code to command line
            &command_history
        );
    } catch (...) {
        print_command_history_executable_code(command_history);
    }

}

TEST_F(WormholeNebulaX2TestFixture, MultithreadedMixedRemoteTransfersLMS) {
    int seed = 0;

    log_info(LogSiliconDriver,"Started MultithreadedMixedRemoteTransfersLMS");

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;
    std::vector<remote_transfer_sample_t> command_history2;
    std::vector<remote_transfer_sample_t> command_history3;
    std::thread t1([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 0.50, .rolled_write = 0., .read = 0.50, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(4, 300000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        );    
    });
    std::thread t2([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            100,

            transfer_type_weights_t{.write = 0.25, .rolled_write = 0.25, .read = 0.50, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history1
        );    
    });
    std::thread t3([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            23,

            transfer_type_weights_t{.write = 0.5, .rolled_write = 0.25, .read = 0.25, .epoch_cmd_write = 0.},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history2
        );    
    });
    std::thread t4([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000 * scale_number_of_tests,
            99,

            transfer_type_weights_t{.write = 1.0, .rolled_write = 0, .read = 0.0, .epoch_cmd_write = 0.0},

            std::uniform_int_distribution<address_t>(0x100000, 0x200000), // address generator distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //ROLLED_WRITE_SIZE_GENERATOR_T const& rolled_write_size_distribution,
            std::uniform_int_distribution<int>(2, 4), //UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution
            std::uniform_int_distribution<transfer_size_t>(0x4, 0x12), //WRITE_EPOCH_CMD_SIZE_GENERATOR_T const& write_epoch_cmd_size_distribution,
            0.75,
            0.75,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000), //READ_SIZE_GENERATOR_T const& read_size_distribution,

            false, // Set to true if you want to emit the command history code to command line
            &command_history3
        );    
    });

    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

TEST_F(WormholeNebulaX2TestFixture, MultithreadedMixedRemoteTransfersLargeWritesSmallReads) {
    int seed = 0;

    log_info(LogSiliconDriver,"Started MultithreadedMixedRemoteTransfersLargeWritesSmallReads");

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;

    auto write_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(1000000, 30000000), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });
    auto read_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(16, 4096), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });

    auto dest_generator = get_default_full_dram_dest_generator(seed, device.get());
    auto address_generator = get_default_address_generator(seed, 0x100000, 0x5000000);

    std::thread write_cmds_thread1([&](){
        RunMixedTransfers(
            *device,
            10000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 1., .rolled_write = 0., .read = 0., .epoch_cmd_write = 0.},

            WriteCommandGenerator(dest_generator, address_generator, write_size_generator),
            build_dummy_rolled_write_command_generator(*device),
            build_dummy_write_epoch_cmd_command_generator(*device),
            build_dummy_read_command_generator(*device),

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        );
    });
    std::thread write_cmds_thread2([&](){
        RunMixedTransfers(
            *device,
            10000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 1., .rolled_write = 0., .read = 0., .epoch_cmd_write = 0.},

            WriteCommandGenerator(dest_generator, address_generator, write_size_generator),
            build_dummy_rolled_write_command_generator(*device),
            build_dummy_write_epoch_cmd_command_generator(*device),
            build_dummy_read_command_generator(*device),

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        );
    });
    std::thread read_cmd_threads1([&](){
        RunMixedTransfers(
            *device,
            10000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 0, .rolled_write = 0., .read = 1., .epoch_cmd_write = 0.},

            build_dummy_write_command_generator(*device),
            build_dummy_rolled_write_command_generator(*device),
            build_dummy_write_epoch_cmd_command_generator(*device),
            ReadCommandGenerator(dest_generator, address_generator, read_size_generator),

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        );
    });
    std::thread read_cmd_threads2([&](){
        RunMixedTransfers(
            *device,
            10000 * scale_number_of_tests,
            0,

            transfer_type_weights_t{.write = 0, .rolled_write = 0., .read = 1., .epoch_cmd_write = 0.},

            build_dummy_write_command_generator(*device),
            build_dummy_rolled_write_command_generator(*device),
            build_dummy_write_epoch_cmd_command_generator(*device),
            ReadCommandGenerator(dest_generator, address_generator, read_size_generator),

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        );
    });

    write_cmds_thread1.join();
    write_cmds_thread2.join();
    read_cmd_threads1.join();
    read_cmd_threads2.join();

}
} // namespace tt::umd::test::utils
