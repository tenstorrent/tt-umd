// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds RISC processor specific API tests for brisc, ncrisc, and other RISC cores.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "test_utils/assembly_programs_for_tests.hpp"
#include "test_utils/setup_risc_cores.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"

using namespace tt::umd;

// This test uses the machine instructions from the header file assembly_programs_for_tests.hpp. How to generate
// this program is explained in the GENERATE_ASSEMBLY_FOR_TESTS.md file.
TEST(TestRiscProgram, DeassertResetBrisc) {
    // The test has large transfers to remote chip, so system memory significantly speeds up the test.
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = get_num_host_ch_for_test()});

    constexpr uint32_t a_variable_value = 0x87654000;
    constexpr uint64_t a_variable_address = 0x10000;
    constexpr uint64_t brisc_code_address = 0x20;

    uint32_t readback = 0;

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;

    std::vector<uint8_t> zero_data(tensix_l1_size, 0);

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            RiscType select_all_tensix_riscv_cores{RiscType::ALL_TENSIX};

            cluster->assert_risc_reset(chip_id, tensix_core, select_all_tensix_riscv_cores);

            cluster->l1_membar(chip_id, {tensix_core});

            // Zero out L1.
            cluster->write_to_device(zero_data.data(), zero_data.size(), chip_id, tensix_core, 0);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(&BRISC_TRAMPOLINE_JMP, sizeof(BRISC_TRAMPOLINE_JMP), chip_id, tensix_core, 0);
            cluster->write_to_device(
                simple_brisc_program.data(),
                simple_brisc_program.size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->read_from_device(&readback, chip_id, tensix_core, a_variable_address, sizeof(readback));

            EXPECT_EQ(a_variable_value, readback)
                << "chip_id: " << chip_id << ", x: " << tensix_core.x << ", y: " << tensix_core.y << "\n";
        }
    }
}

TEST(TestRiscProgram, DeassertResetWithCounterBrisc) {
    // The test has large transfers to remote chip, so system memory significantly speeds up the test.
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = get_num_host_ch_for_test()});

    // TODO: remove this check when it is figured out what is happening with Blackhole version of this test.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole architecture, as it seems flaky for Blackhole.";
    }

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;
    std::vector<uint32_t> zero_data(tensix_l1_size / sizeof(uint32_t), 0);

    constexpr uint64_t counter_address = 0x10000;
    constexpr uint64_t brisc_code_address = 0x20;

    uint32_t first_readback_value = 0;
    uint32_t second_readback_value = 0;

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        for (const CoreCoord& tensix_core : tensix_cores) {
            cluster->write_to_device(zero_data.data(), zero_data.size() * sizeof(uint32_t), chip_id, tensix_core, 0x0);

            cluster->l1_membar(chip_id, {tensix_core});

            RiscType select_all_tensix_riscv_cores{RiscType::ALL_TENSIX};

            cluster->assert_risc_reset(chip_id, tensix_core, select_all_tensix_riscv_cores);

            cluster->write_to_device(&BRISC_TRAMPOLINE_JMP, sizeof(BRISC_TRAMPOLINE_JMP), chip_id, tensix_core, 0);
            cluster->write_to_device(
                counter_brisc_program.data(),
                counter_brisc_program.size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            cluster->read_from_device(
                &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

            cluster->read_from_device(
                &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

            // Since we expect BRISC to work and constantly increment counter in L1, we expect values to be different on
            // two reads from device
            EXPECT_NE(second_readback_value, first_readback_value);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->assert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            cluster->read_from_device(
                &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

            cluster->read_from_device(
                &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

            // When the BRISC is in reset state the counter is not incremented in L1, and we expect values to be
            // different on two reads from device
            EXPECT_EQ(second_readback_value, first_readback_value);
        }
    }
}

TEST_P(ClusterAssertDeassertRiscsTest, TriscNcriscAssertDeassertTest) {
    // The test has large transfers to remote chip, so system memory significantly speeds up the test.
    std::unique_ptr<Cluster> cluster =
        std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = get_num_host_ch_for_test()});

    // TODO: remove this check when it is figured out what is happening with Blackhole version of this test.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::BLACKHOLE) {
        GTEST_SKIP() << "Skipping test for Blackhole architecture, as it seems flaky for Blackhole.";
    }

    // TODO: remove this check when it is figured out what is happening with llmbox.
    if (cluster->get_tt_device(0)->get_arch() == tt::ARCH::WORMHOLE_B0 &&
        cluster->get_target_device_ids().size() == 8) {
        GTEST_SKIP() << "Skipping test for LLMBox architecture, as it seems flaky.";
    }

    auto get_brisc_configuration_program_for_chip = [](Cluster* cluster,
                                                       ChipId chip_id) -> std::optional<std::array<uint32_t, 14>> {
        switch (cluster->get_cluster_description()->get_arch(chip_id)) {
            case tt::ARCH::WORMHOLE_B0:
                return std::make_optional(wh_brisc_configuration_program);
            case tt::ARCH::BLACKHOLE:
                return std::make_optional(bh_brisc_configuration_program);
            default:
                return std::nullopt;
        }
    };

    const auto& configurations_of_risc_cores = GetParam();

    constexpr uint64_t brisc_code_address = 0x20;

    uint32_t first_readback_value = 0;
    uint32_t second_readback_value = 0;

    auto tensix_l1_size = cluster->get_soc_descriptor(0).worker_l1_size;
    std::vector<uint32_t> zero_data(tensix_l1_size / sizeof(uint32_t), 0);

    auto chip_ids = cluster->get_target_device_ids();
    for (auto& chip_id : chip_ids) {
        auto brisc_configuration_program = get_brisc_configuration_program_for_chip(cluster.get(), chip_id);

        if (!brisc_configuration_program) {
            GTEST_SKIP() << "Unsupported architecture for deassert test.";
        }

        auto tensix_cores = cluster->get_soc_descriptor(chip_id).get_cores(CoreType::TENSIX);

        RiscType risc_cores{RiscType::NONE};

        for (const CoreCoord& tensix_core : tensix_cores) {
            cluster->assert_risc_reset(chip_id, tensix_core, RiscType::ALL_TENSIX);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(zero_data.data(), zero_data.size() * sizeof(uint32_t), chip_id, tensix_core, 0x0);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->write_to_device(&BRISC_TRAMPOLINE_JMP, sizeof(BRISC_TRAMPOLINE_JMP), chip_id, tensix_core, 0);
            cluster->write_to_device(
                brisc_configuration_program.value().data(),
                brisc_configuration_program.value().size() * sizeof(uint32_t),
                chip_id,
                tensix_core,
                brisc_code_address);

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, RiscType::BRISC);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto& [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;
                risc_cores = risc_cores | risc_core;

                cluster->write_to_device(
                    code_program.data(), code_program.size() * sizeof(uint32_t), chip_id, tensix_core, code_address);
            }

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->deassert_risc_reset(chip_id, tensix_core, risc_cores);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto& [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;

                cluster->read_from_device(
                    &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

                cluster->read_from_device(
                    &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

                EXPECT_NE(first_readback_value, second_readback_value);
            }

            cluster->l1_membar(chip_id, {tensix_core});

            cluster->assert_risc_reset(chip_id, tensix_core, risc_cores);

            for (const auto& configuration_of_risc_core : configurations_of_risc_cores) {
                auto [code_address, counter_address, code_program, risc_core] = configuration_of_risc_core;

                cluster->read_from_device(
                    &first_readback_value, chip_id, tensix_core, counter_address, sizeof(first_readback_value));

                cluster->read_from_device(
                    &second_readback_value, chip_id, tensix_core, counter_address, sizeof(second_readback_value));

                EXPECT_EQ(first_readback_value, second_readback_value);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllTriscNcriscCoreCombinations,
    ClusterAssertDeassertRiscsTest,
    ::testing::ValuesIn(ClusterAssertDeassertRiscsTest::generate_all_risc_cores_combinations()));

TEST(TestRiscProgram, StartDeviceWithValidRiscProgram) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(ClusterOptions{.num_host_mem_ch_per_mmio_device = 1});
    constexpr uint64_t write_address = 0x1000;

    test_utils::safe_test_cluster_start(cluster.get());

    // Initialize random data.
    size_t data_size = 1024;
    std::vector<uint8_t> data(data_size, 0);
    for (int i = 0; i < data_size; i++) {
        data[i] = i % 256;
    }

    for (auto chip_id : cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        cluster->write_to_device(data.data(), data_size, chip_id, any_core, write_address);

        cluster->wait_for_non_mmio_flush(chip_id);
    }

    // Now read back the data.
    for (auto chip_id : cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);

        const CoreCoord any_core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::vector<uint8_t> readback_data(data_size, 0);
        cluster->read_from_device(readback_data.data(), chip_id, any_core, write_address, data_size);

        ASSERT_EQ(data, readback_data);
    }

    cluster->close_device();
}

// Basic write/read loopback on the first TENSIX core followed by assert/deassert
// of a variety of RiscType masks (ALL_TENSIX, ALL_NEO_DMS, BRISC, custom DM bitmask).
// Sim-only: silicon liveness validation would require an arch-specific RISC program;
// this only confirms the API accepts the masks without throwing.
TEST(TestRiscProgram, SimpleApiTest) {
    if (!is_simulation_test()) {
        GTEST_SKIP() << "SimpleApiTest is currently sim-only.";
    }

    std::unique_ptr<Cluster> cluster = test_utils::make_default_test_cluster();

    for (auto chip_id : cluster->get_target_device_ids()) {
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(chip_id);
        const CoreCoord core = soc_desc.get_cores(CoreType::TENSIX)[0];

        std::vector<uint32_t> wdata = {1, 2, 3, 4, 5};
        std::vector<uint32_t> rdata(wdata.size(), 0);

        cluster->write_to_device(wdata.data(), wdata.size() * sizeof(uint32_t), chip_id, core, 0x100);
        cluster->read_from_device(rdata.data(), chip_id, core, 0x100, rdata.size() * sizeof(uint32_t));
        ASSERT_EQ(wdata, rdata);

        cluster->assert_risc_reset(chip_id, core, RiscType::ALL_TENSIX);
        cluster->assert_risc_reset(chip_id, core, RiscType::ALL_NEO_DMS);
        cluster->deassert_risc_reset(chip_id, core, RiscType::BRISC, /*staggered_start=*/true);
        cluster->deassert_risc_reset(chip_id, core, RiscType::ALL_NEO_DMS, /*staggered_start=*/true);

        const RiscType example_dm_cores = RiscType::DM0 | RiscType::DM1 | RiscType::DM7;
        cluster->assert_risc_reset(chip_id, core, example_dm_cores);
        cluster->deassert_risc_reset(chip_id, core, example_dm_cores, /*staggered_start=*/true);
    }
}
