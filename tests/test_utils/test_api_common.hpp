// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

#include "test_utils/assembly_programs_for_tests.hpp"
#include "umd/device/cluster.hpp"

using namespace tt::umd;

using RiscCoreProgramConfig = std::tuple<uint64_t, uint32_t, std::array<uint32_t, 6>, RiscType>;
using RiscSetUnderTest = std::vector<RiscCoreProgramConfig>;

class ClusterAssertDeassertRiscsTest : public ::testing::TestWithParam<RiscSetUnderTest> {
public:
    static constexpr uint64_t trisc0_code_address = 0x20000;
    static constexpr uint64_t trisc1_code_address = 0x30000;
    static constexpr uint64_t trisc2_code_address = 0x40000;
    static constexpr uint64_t ncrisc_code_address = 0x50000;

    static constexpr uint32_t trisc0_counter_address = 0x2000;
    static constexpr uint32_t trisc1_counter_address = 0x3000;
    static constexpr uint32_t trisc2_counter_address = 0x4000;
    static constexpr uint32_t ncrisc_counter_address = 0x5000;

    static constexpr uint32_t register_instruction = 0x737;

    static std::vector<RiscSetUnderTest> generate_all_risc_cores_combinations() {
        // This lambda has the same program as counter_brisc_program and it changes the location
        // where the counter is stored.
        // Note: This address must have the first 4 nibbles set to 0 as the machine instruction used is lui
        // which expects this behavior
        constexpr auto make_counter_program = [](uint32_t counter_address_instruction) constexpr {
            std::array<uint32_t, 6> instructions = {counter_brisc_program};  // first element is a placeholder
            instructions[0] = counter_address_instruction;
            return instructions;
        };

        constexpr std::array<uint32_t, 6> trisc0_program{
            make_counter_program(trisc0_counter_address | register_instruction)};
        constexpr std::array<uint32_t, 6> trisc1_program{
            make_counter_program(trisc1_counter_address | register_instruction)};
        constexpr std::array<uint32_t, 6> trisc2_program{
            make_counter_program(trisc2_counter_address | register_instruction)};
        constexpr std::array<uint32_t, 6> ncrisc_program{
            make_counter_program(ncrisc_counter_address | register_instruction)};

        std::vector<RiscCoreProgramConfig> triscs_and_ncrisc{
            {trisc0_code_address, trisc0_counter_address, trisc0_program, RiscType::TRISC0},
            {trisc1_code_address, trisc1_counter_address, trisc1_program, RiscType::TRISC1},
            {trisc2_code_address, trisc2_counter_address, trisc2_program, RiscType::TRISC2},
            {ncrisc_code_address, ncrisc_counter_address, ncrisc_program, RiscType::NCRISC}};

        const auto all_trisc_and_ncrisc_combinations = generate_all_non_empty_risc_core_combinations(triscs_and_ncrisc);

        return all_trisc_and_ncrisc_combinations;
    }

private:
    static std::vector<RiscSetUnderTest> generate_all_non_empty_risc_core_combinations(
        const std::vector<RiscCoreProgramConfig>& cores) {
        std::vector<RiscSetUnderTest> risc_core_combinations;
        const size_t n = cores.size();

        for (size_t bitmask = 1; bitmask < (1 << n); ++bitmask) {
            RiscSetUnderTest risc_core_subset;
            for (size_t i = 0; i < n; ++i) {
                if (bitmask & (1 << i)) {
                    risc_core_subset.push_back(cores[i]);
                }
            }
            risc_core_combinations.push_back(std::move(risc_core_subset));
        }
        return risc_core_combinations;
    }
};

// Helper function to detect if the cluster is a Galaxy configuration, including 4U and 6U configurations.
inline bool is_galaxy_configuration(Cluster* cluster) {
    return !cluster->get_target_device_ids().empty() &&
           (cluster->get_cluster_description()->get_board_type(0) == tt::BoardType::UBB_WORMHOLE ||
            cluster->get_cluster_description()->get_board_type(0) == tt::BoardType::UBB_BLACKHOLE);
}

// Returns the top-left (lowest x, lowest y) and bottom-right (highest x, highest y) TENSIX cores
// in translated coordinates for the given SoC descriptor.
inline std::vector<CoreCoord> get_tensix_corners(const SocDescriptor& soc_desc) {
    const auto cores = soc_desc.get_cores(tt::CoreType::TENSIX, tt::CoordSystem::TRANSLATED);
    if (cores.empty()) {
        throw std::runtime_error("No TENSIX cores found in SoC descriptor");
    }
    CoreCoord top_left = cores[0];
    CoreCoord bottom_right = cores[0];
    for (const auto& core : cores) {
        if (core.x < top_left.x || core.y < top_left.y) {
            top_left = core;
        }
        if (core.x > bottom_right.x || core.y > bottom_right.y) {
            bottom_right = core;
        }
    }
    return {top_left, bottom_right};
}

class ClusterReadWriteL1Test : public ::testing::TestWithParam<ClusterOptions> {};

// Safe L1 address for use in API tests. Low addresses (e.g. 0x10) are reserved on Blackhole
// by ARC firmware (doppler throttle state), so tests must start at or above this address.
// In some cases you also have to be careful about not overwriting the membar address.
constexpr uint64_t SAFE_IO_L1_ADDRESS = 0x1000;

// True when the test should run against a simulator, indicated by TT_UMD_SIMULATOR
// pointing at the simulator binary directory.
inline bool is_simulation_test() { return std::getenv("TT_UMD_SIMULATOR") != nullptr; }
