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
#include "test_wh_common.h"

#include <memory>



namespace tt::umd::test::utils {
class WormholeNebulaX2TestFixture : public WormholeTestFixture {
 private:
  static int detected_num_chips;
  static bool skip_tests;

 protected: 

  static constexpr int EXPECTED_NUM_CHIPS = 2;

  static void SetUpTestSuite() {
    std::unique_ptr<tt_ClusterDescriptor> cluster_desc = tt_ClusterDescriptor::create_from_yaml(GetClusterDescYAML().string());
    detected_num_chips = cluster_desc->get_number_of_chips();
    if (detected_num_chips != EXPECTED_NUM_CHIPS) {
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

int WormholeNebulaX2TestFixture::detected_num_chips = -1;
bool WormholeNebulaX2TestFixture::skip_tests = false;


TEST_F(WormholeNebulaX2TestFixture, MixedRemoteTransfersMediumSmall) {
    int seed = 0;

    // std::cout << "Running commands." << std::endl;
    std::vector<remote_transfer_sample_t> command_history;
    try {
        assert(device != nullptr);
        RunMixedTransfersUniformDistributions(
            *device, 
            100000,
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

    // std::cout << "Running commands in multithreaded mode." << std::endl;

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;
    std::vector<remote_transfer_sample_t> command_history2;
    std::vector<remote_transfer_sample_t> command_history3;
    std::thread t1([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000,
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
            100000,
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
            100000,
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
            100000,
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

    // std::cout << "Running commands." << std::endl;
    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history;
    try {
        RunMixedTransfersUniformDistributions(
            *device, 
            10000,
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

    // std::cout << "Running commands." << std::endl;
    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history;

    auto write_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, double, std::normal_distribution>(
        seed, std::normal_distribution<>(10000, 3000), [](double x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });
    

    auto dest_generator = get_default_full_dram_dest_generator(seed, device.get());
    auto address_generator = get_default_address_generator(seed, 0x100000, 0x5000000);

    try {
        RunMixedTransfers(
            *device, 
            10000,
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

    // std::cout << "Running commands in multithreaded mode." << std::endl;

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;
    std::vector<remote_transfer_sample_t> command_history2;
    std::vector<remote_transfer_sample_t> command_history3;
    std::thread t1([&](){
        RunMixedTransfersUniformDistributions(
            *device, 
            100000,
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
            100000,
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
            100000,
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
            100000,
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

    // std::cout << "Running commands in multithreaded mode." << std::endl;

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;

    auto write_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(1000000, 30000000), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });
    auto read_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(16, 4096), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });

    auto dest_generator = get_default_full_dram_dest_generator(seed, device.get());
    auto address_generator = get_default_address_generator(seed, 0x100000, 0x5000000);

    std::thread write_cmds_thread([&](){
        RunMixedTransfers(
            *device, 
            10000,
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
    std::thread read_cmd_threads([&](){
        RunMixedTransfers(
            *device, 
            10000,
            0,

            transfer_type_weights_t{.write = 0., .rolled_write = 0., .read = 1., .epoch_cmd_write = 0.},

            build_dummy_write_command_generator(*device),
            build_dummy_rolled_write_command_generator(*device),
            build_dummy_write_epoch_cmd_command_generator(*device),
            ReadCommandGenerator(dest_generator, address_generator, read_size_generator),

            false, // Set to true if you want to emit the command history code to command line
            &command_history0
        ); 
    });

    write_cmds_thread.join();
    read_cmd_threads.join();
}


TEST_F(WormholeNebulaX2TestFixture, MultithreadedMixedRemoteTransfersSmallWritesWithReadback) {
    int seed = 0;

    // std::cout << "Running commands in multithreaded mode." << std::endl;

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;

    auto write_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(4, 1024), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });

    auto dest_generator = get_default_full_dram_dest_generator(seed, device.get());
    auto address_generator = get_default_address_generator(seed, 0x100000, 0x5000000);

    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    int num_chips = device->get_number_of_chips_in_cluster();

    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build_default(soc_desc, {0,1});

    counting_barrier_t writer_sync_barrier = counting_barrier_t(1);

    std::thread write_cmds_thread([&](){
        RunWriteTransfers(
            *device, 
            10000,
            0,

            std::unique_ptr<CommandSampler>(new WriteCommandSampler(dest_generator, /*address_generator,*/ write_size_generator)),
            readback_write_generator,
            writer_sync_barrier

        );
    });
    std::thread read_cmd_threads([&](){
        RunReadbackChecker(*device, 0, readback_write_generator); 
    });

    write_cmds_thread.join();
    read_cmd_threads.join();
}


TEST_F(WormholeNebulaX2TestFixture, MultithreadedMixedRemoteTransfers1MBto30MBWritesWithReadback) {
    int seed = 0;

    assert(device != nullptr);
    std::vector<remote_transfer_sample_t> command_history0;
    std::vector<remote_transfer_sample_t> command_history1;

    auto write_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(1000000, 30000000), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });
    auto read_size_generator = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>(
        seed, std::uniform_int_distribution<transfer_size_t>(16, 4096), [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); });

    auto dest_generator = get_default_full_dram_dest_generator(seed, device.get());
    auto address_generator = get_default_address_generator(seed, 0x100000, 0x5000000);

    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    int num_chips = device->get_number_of_chips_in_cluster();

    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build_default(soc_desc, {0,1});

    counting_barrier_t writer_sync_barrier = counting_barrier_t(1);

    std::thread write_cmds_thread([&](){
        RunWriteTransfers(
            *device, 
            500,
            0,

            std::unique_ptr<CommandSampler>(new WriteCommandSampler(dest_generator, /*address_generator,*/ write_size_generator)),
            readback_write_generator,
            writer_sync_barrier

        );
    });
    std::thread read_cmd_threads([&](){
        RunReadbackChecker(*device, 0, readback_write_generator); 
    });

    write_cmds_thread.join();
    read_cmd_threads.join();
}

TEST_F(WormholeNebulaX2TestFixture, Multithreaded4Bto1024BWritesWithReadback2Writers2Readers) {
    static constexpr int NUM_WRITERS = 2;
    static constexpr int NUM_READERS = 2;
    assert(device != nullptr);
    
    using size_generator_t = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>;
    auto size_aligner = [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); };
    auto write_size_distribution = std::uniform_int_distribution<transfer_size_t>(4, 1024);

    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    int num_chips = device->get_number_of_chips_in_cluster();
    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build_default(soc_desc, {0,1});

    counting_barrier_t writer_sync_barrier = counting_barrier_t(NUM_WRITERS);
    std::vector<std::thread> write_cmd_threads;
    for (int i = 0; i < NUM_WRITERS; i++) {
        write_cmd_threads.emplace_back([&write_size_distribution,i,this,size_aligner,&readback_write_generator,&writer_sync_barrier](){
            RunWriteTransfers(
                *device, 10000, i,

                std::unique_ptr<CommandSampler>(new WriteCommandSampler(
                    get_default_full_dram_dest_generator(i, device.get()),  // dest_generator
                    size_generator_t(i, write_size_distribution, size_aligner) // size generator
                )),
                readback_write_generator,
                writer_sync_barrier
            );
        });
    }


    std::vector<std::thread> readback_threads;
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.emplace_back([&](){
            RunReadbackChecker(*device, 0, readback_write_generator); 
        });
    }

    for (int i = 0; i < NUM_WRITERS; i++) {
        write_cmd_threads.at(i).join();
    }
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.at(i).join();
    }
}

TEST_F(WormholeNebulaX2TestFixture, Multithreaded4Bto1024B_1Kto1MB_16MBto128MB_WritesWithReadback3Writers4Readers) {
    //
    // Worst case, this should take 
    // 5000 (small writes) * 1024 (max small wr size) = 5MB
    // + 1000 (medium writes) * 1MB (max medium wr size) = 1GB
    // + 200 (large writes) * 128MB (max large wr size) = 25.6GB
    // = 5MB + 1GB + 25.6GB ~= 26.6GB written and readback
    // => ~ 8.8hrs at 100MB/s read and write
    
    static constexpr int NUM_WRITERS = 3;
    static constexpr int NUM_READERS = 4;
    assert(device != nullptr);
    
    using size_generator_t = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>;
    auto size_aligner = [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); };
    auto small_write_size_distribution = std::uniform_int_distribution<transfer_size_t>(4, 1024);
    auto medium_write_size_distribution = std::uniform_int_distribution<transfer_size_t>(1024, 1 * 1024 * 1024);
    auto large_write_size_distribution = std::uniform_int_distribution<transfer_size_t>(16 * 1024 * 1024, 128 * 1024 * 1024);

    std::vector<std::uniform_int_distribution<transfer_size_t>> size_distributions = {
        small_write_size_distribution,
        medium_write_size_distribution,
        large_write_size_distribution
    };
    assert (size_distributions.size() == NUM_WRITERS);

    std::vector<int> sample_counts = {5000, 1000, 200};
    assert (sample_counts.size() == NUM_WRITERS);

    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    int num_chips = device->get_number_of_chips_in_cluster();
    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build_default(soc_desc, {0,1});

    counting_barrier_t writer_sync_barrier = counting_barrier_t(NUM_WRITERS);
    std::vector<std::thread> write_cmd_threads;
    for (int i = 0; i < NUM_WRITERS; i++) {
        // auto &size_distribution = size_distributions.at(i);
        // int num_samples = sample_counts.at(i);
        write_cmd_threads.emplace_back([&sample_counts,&size_distributions,i,this,size_aligner,&readback_write_generator,&writer_sync_barrier](){
            RunWriteTransfers(
                *device, sample_counts.at(i), i,

                std::unique_ptr<CommandSampler>(new WriteCommandSampler(
                    get_default_full_dram_dest_generator(i, device.get()),  // dest_generator
                    size_generator_t(i, size_distributions.at(i), size_aligner) // size generator
                )),
                readback_write_generator,
                writer_sync_barrier
            );
        });
    }


    std::vector<std::thread> readback_threads;
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.emplace_back([&](){
            RunReadbackChecker(*device, 0, readback_write_generator); 
        });
    }

    for (auto &t : write_cmd_threads) {
        t.join();
    }
    for (auto &t : readback_threads) {
        t.join();
    }
}


TEST_F(WormholeNebulaX2TestFixture, Multithreaded4Bto1024BWritesWithReadback16Writers1Readers) {
    static constexpr int NUM_WRITERS = 16;
    static constexpr int NUM_READERS = 1;
    assert(device != nullptr);
    
    using size_generator_t = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>;
    auto size_aligner = [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); };
    auto write_size_distribution = std::uniform_int_distribution<transfer_size_t>(4, 1024);

    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    int num_chips = device->get_number_of_chips_in_cluster();
    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build_default(soc_desc, {0,1});

    counting_barrier_t writer_sync_barrier = counting_barrier_t(NUM_WRITERS);
    std::vector<std::thread> write_cmd_threads;
    for (int i = 0; i < NUM_WRITERS; i++) {
        write_cmd_threads.emplace_back([&write_size_distribution,i,this,size_aligner,&readback_write_generator,&writer_sync_barrier](){
            RunWriteTransfers(
                *(this->device), 2000, i,

                std::unique_ptr<CommandSampler>(new WriteCommandSampler(
                    get_default_full_dram_dest_generator(i, this->device.get()),  // dest_generator
                    size_generator_t(i, write_size_distribution, size_aligner) // size generator
                )),
                readback_write_generator,
                writer_sync_barrier
            );
        });
    }


    std::vector<std::thread> readback_threads;
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.emplace_back([&](){
            RunReadbackChecker(*device, 0, readback_write_generator); 
        });
    }

    for (int i = 0; i < NUM_WRITERS; i++) {
        write_cmd_threads.at(i).join();
    }
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.at(i).join();
    }
}



// This was an overkill test, mainly for exercising the test infra itself
TEST_F(WormholeNebulaX2TestFixture, DISABLED_MultithreadedSmallWritesWithReadback128Writers16Readers) {
    static constexpr int NUM_WRITERS = 128;
    static constexpr int NUM_READERS = 16;
    assert(device != nullptr);
    
    using size_generator_t = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>;
    auto size_aligner = [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); };
    auto write_size_distribution = std::uniform_int_distribution<transfer_size_t>(4, 1024);

    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    int num_chips = device->get_number_of_chips_in_cluster();
    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build_default(soc_desc, {0,1});

    counting_barrier_t writer_sync_barrier = counting_barrier_t(NUM_WRITERS);
    std::vector<std::thread> write_cmd_threads;
    for (int i = 0; i < NUM_WRITERS; i++) {
        write_cmd_threads.emplace_back([&write_size_distribution,i,this,size_aligner,&readback_write_generator,&writer_sync_barrier](){
            RunWriteTransfers(
                *device, 100, i,

                std::unique_ptr<CommandSampler>(new WriteCommandSampler(
                    get_default_full_dram_dest_generator(i, device.get()),  // dest_generator
                    size_generator_t(i, write_size_distribution, size_aligner) // size generator
                )),
                readback_write_generator,
                writer_sync_barrier
            );
        });
    }

    std::vector<std::thread> readback_threads;
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.emplace_back([&](){
            RunReadbackChecker(*device, 0, readback_write_generator); 
        });
    }

    for (int i = 0; i < NUM_WRITERS; i++) {
        write_cmd_threads.at(i).join();
    }
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.at(i).join();
    }
}

TEST_F(WormholeNebulaX2TestFixture, DISABLED_MultithreadedMixedSmallMediumWritesWriteAndReadBackToBack1WritersAndReaderPerDramChannel) {
    tt_SocDescriptor const& soc_desc = device->get_virtual_soc_descriptors().at(0);
    auto num_dram_channels = soc_desc.dram_cores.size();
    static const int NUM_WRITERS = num_dram_channels;
    static const int NUM_READERS = num_dram_channels;
    assert(device != nullptr);
    
    using size_generator_t = ConstrainedTemplateTemplateGenerator<transfer_size_t, transfer_size_t, std::uniform_int_distribution>;
    auto size_aligner = [](transfer_size_t x) -> transfer_size_t { return size_aligner_32B(static_cast<transfer_size_t>((x >= 4) ? x : 4)); };
    auto small_write_size_distribution = std::uniform_int_distribution<transfer_size_t>(4, 1024);
    auto medium_write_size_distribution = std::uniform_int_distribution<transfer_size_t>(1024, 1 * 1024 * 1024);

    std::vector<std::uniform_int_distribution<transfer_size_t>> size_distributions = {
        small_write_size_distribution,
        medium_write_size_distribution
    };

    std::vector<int> sample_counts = {5000, 1000};
    std::unordered_set<chip_id_t> chip_ids = {1};

    int num_chips = device->get_number_of_chips_in_cluster();
    auto readback_write_generator = ThreadSafeNonOverlappingWriteAddressGenerator::build(soc_desc, chip_ids, 1, 2);

    counting_barrier_t writer_sync_barrier = counting_barrier_t(NUM_WRITERS);
    std::vector<std::thread> write_cmd_threads;
    for (int i = 0; i < NUM_WRITERS; i++) {
        // auto &size_distribution = size_distributions.at(i % size_distributions.size());
        // int num_samples = sample_counts.at(i % sample_counts.size());
        write_cmd_threads.emplace_back([&sample_counts,&size_distributions,i,this,size_aligner,chip_ids,&readback_write_generator,&writer_sync_barrier](){
            RunWriteTransfers(
                *(this->device), sample_counts.at(i % sample_counts.size()), i,

                std::unique_ptr<CommandSampler>(new WriteCommandSampler(
                    get_full_dram_dest_generator(i, device.get(), chip_ids),  // dest_generator
                    size_generator_t(i, size_distributions.at(i % size_distributions.size()), size_aligner) // size generator
                )),
                readback_write_generator,
                writer_sync_barrier
            );
        });
    }


    std::vector<std::thread> readback_threads;
    for (int i = 0; i < NUM_READERS; i++) {
        readback_threads.emplace_back([&](){
            RunReadbackChecker(*device, 0, readback_write_generator); 
        });
    }

    for (auto &t : write_cmd_threads) {
        t.join();
    }
    for (auto &t : readback_threads) {
        t.join();
    }
}

} // namespace tt::umd::test::utils