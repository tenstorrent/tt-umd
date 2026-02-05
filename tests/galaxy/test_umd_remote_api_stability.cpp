// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <tt-logger/tt-logger.hpp>
#include <vector>

#include "tests/galaxy/test_galaxy_common.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/stimulus_generators.hpp"
#include "tests/wormhole/test_wh_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "wormhole/eth_interface.h"
#include "wormhole/host_mem_address_map.h"
#include "wormhole/l1_address_map.h"

namespace tt::umd::test::utils {

class WormholeGalaxyStabilityTestFixture : public WormholeTestFixture {
private:
    static int detected_num_chips;
    static bool skip_tests;

protected:
    static constexpr int EXPECTED_MIN_CHIPS = 32;
    static uint32_t scale_number_of_tests;

    static void SetUpTestSuite() {
        std::unique_ptr<ClusterDescriptor> cluster_desc = Cluster::create_cluster_descriptor();
        detected_num_chips = cluster_desc->get_number_of_chips();
        if (detected_num_chips < EXPECTED_MIN_CHIPS) {
            skip_tests = true;
        }
        if (char const* scale_number_of_tests_env = std::getenv("SCALE_NUMBER_OF_TESTS")) {
            scale_number_of_tests = std::atoi(scale_number_of_tests_env);
        }
    }

    int get_detected_num_chips() override { return detected_num_chips; }

    bool is_test_skipped() override { return skip_tests; }
};

int WormholeGalaxyStabilityTestFixture::detected_num_chips = -1;
bool WormholeGalaxyStabilityTestFixture::skip_tests = false;
uint32_t WormholeGalaxyStabilityTestFixture::scale_number_of_tests = 1;

TEST_F(WormholeGalaxyStabilityTestFixture, MixedRemoteTransfers) {
    int const seed = 0;

    assert(cluster != nullptr);
    log_info(LogUMD, "Started MixedRemoteTransfers");
    std::vector<remote_transfer_sample_t> command_history;
    try {
        RunMixedTransfersUniformDistributions(
            *this->cluster,
            100000 * scale_number_of_tests,
            seed,
            transfer_type_weights_t{.write = 0.40, .read = 0.4},
            // address generator distribution
            std::uniform_int_distribution<address_t>(0x100000, 0x200000),
            // WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            // UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution.
            std::uniform_int_distribution<int>(2, 4),
            0.75,
            0.75,
            // READ_SIZE_GENERATOR_T const& read_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            // Set to true if you want to emit the command history code to command line.
            false,
            &command_history);
    } catch (...) {
        print_command_history_executable_code(command_history);
    }
}

TEST_F(WormholeGalaxyStabilityTestFixture, DISABLED_MultithreadedMixedRemoteTransfersMediumSmall) {
    log_info(LogUMD, "Started MultithreadedMixedRemoteTransfersMediumSmall");

    assert(cluster != nullptr);
    std::thread t1([&]() {
        RunMixedTransfersUniformDistributions(
            *cluster,
            50000 * scale_number_of_tests,
            0,
            transfer_type_weights_t{.write = 0.50, .read = 0.50},
            // address generator distribution
            std::uniform_int_distribution<address_t>(0x100000, 0x200000),
            // WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            // UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution.
            std::uniform_int_distribution<int>(2, 4),
            0.75,
            0.75,
            // READ_SIZE_GENERATOR_T const& read_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            // Set to true if you want to emit the command history code to command line.
            false,
            nullptr);
    });
    std::thread t2([&]() {
        RunMixedTransfersUniformDistributions(
            *cluster,
            50000 * scale_number_of_tests,
            100,
            transfer_type_weights_t{.write = 0.25, .read = 0.50},
            // address generator distribution
            std::uniform_int_distribution<address_t>(0x100000, 0x200000),
            // WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            // UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution.
            std::uniform_int_distribution<int>(2, 4),
            0.75,
            0.75,
            // READ_SIZE_GENERATOR_T const& read_size_distribution,
            // Set to true if you want to emit the command history code to command line.
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            false,
            nullptr);
    });
    std::thread t3([&]() {
        RunMixedTransfersUniformDistributions(
            *cluster,
            50000 * scale_number_of_tests,
            23,
            transfer_type_weights_t{.write = 0.5, .read = 0.25},
            // address generator distribution
            std::uniform_int_distribution<address_t>(0x100000, 0x200000),
            // WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            // UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution.
            std::uniform_int_distribution<int>(2, 4),
            0.75,
            0.75,
            // READ_SIZE_GENERATOR_T const& read_size_distribution,
            // Set to true if you want to emit the command history code to command line.
            std::uniform_int_distribution<transfer_size_t>(0x4, 30000),
            false,
            nullptr);
    });
    std::thread t4([&]() {
        RunMixedTransfersUniformDistributions(
            *cluster,
            100000 * scale_number_of_tests,
            99,
            transfer_type_weights_t{.write = 0.1, .read = 0.1},
            // address generator distribution
            std::uniform_int_distribution<address_t>(0x100000, 0x200000),
            // WRITE_SIZE_GENERATOR_T const& write_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000),
            // UNROLL_COUNT_GENERATOR_T const& unroll_count_distribution.
            std::uniform_int_distribution<int>(2, 4),
            0.75,
            0.75,
            // READ_SIZE_GENERATOR_T const& read_size_distribution,
            std::uniform_int_distribution<transfer_size_t>(0x4, 3000),
            // Set to true if you want to emit the command history code to command line.
            false,
            nullptr);
    });

    t1.join();
    t2.join();
    t3.join();
    t4.join();
}

}  // namespace tt::umd::test::utils
