// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#pragma once

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "test_utils/assembly_programs_for_tests.hpp"
#include "umd/device/cluster.h"
#include "umd/device/tt_silicon_driver_common.hpp"

using namespace tt::umd;

using RiscCoreProgramConfig = std::tuple<uint64_t, uint32_t, std::array<uint32_t, 6>, TensixSoftResetOptions>;
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
            {trisc0_code_address, trisc0_counter_address, trisc0_program, TensixSoftResetOptions::TRISC0},
            {trisc1_code_address, trisc1_counter_address, trisc1_program, TensixSoftResetOptions::TRISC1},
            {trisc2_code_address, trisc2_counter_address, trisc2_program, TensixSoftResetOptions::TRISC2},
            {ncrisc_code_address, ncrisc_counter_address, ncrisc_program, TensixSoftResetOptions::NCRISC}};

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

class ClusterReadWriteL1Test : public ::testing::TestWithParam<ClusterOptions> {};
