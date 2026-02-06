// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// This file holds Cluster specific API examples.

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>  // For access()

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>  // for std::getenv
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "test_utils/assembly_programs_for_tests.hpp"
#include "test_utils/setup_risc_cores.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "tests/test_utils/fetch_local_files.hpp"
#include "tests/test_utils/test_api_common.hpp"
#include "umd/device/arch/blackhole_implementation.hpp"
#include "umd/device/arch/grendel_implementation.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/chip/local_chip.hpp"
#include "umd/device/chip/mock_chip.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/cluster_descriptor.hpp"
#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/warm_reset.hpp"
#include "utils.hpp"

using namespace tt::umd;

// These tests are intended to be run with the same code on all kinds of systems:
// N150. N300
// Galaxy.

std::vector<ClusterOptions> get_cluster_options_for_param_test() {
    constexpr const char* TT_UMD_SIMULATOR_ENV = "TT_UMD_SIMULATOR";
    std::vector<ClusterOptions> options;
    options.push_back(ClusterOptions{.chip_type = ChipType::SILICON});
    if (std::getenv(TT_UMD_SIMULATOR_ENV)) {
        options.push_back(ClusterOptions{
            .chip_type = ChipType::SIMULATION,
            .target_devices = {0},
            .simulator_directory = std::filesystem::path(std::getenv(TT_UMD_SIMULATOR_ENV))});
    }
    return options;
}

// Small helper function to check if the ipmitool is ready.
bool is_ipmitool_ready() {
    if (system("which ipmitool > /dev/null 2>&1") != 0) {
        std::cout << "ipmitool executable not found." << std::endl;
        return false;
    }

    if ((access("/dev/ipmi0", F_OK) != 0) && (access("/dev/ipmi/0", F_OK) != 0) &&
        (access("/dev/ipmidev/0", F_OK) != 0)) {
        std::cout << "IPMI device file not found (/dev/ipmi0, /dev/ipmi/0, or /dev/ipmidev/0)." << std::endl;
        return false;
    }

    return true;
}

TEST(TestCluster, WarmResetScratch) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();

    if (cluster->get_target_device_ids().empty()) {
        GTEST_SKIP() << "No chips present on the system. Skipping test.";
    }

    if (is_galaxy_configuration(cluster.get())) {
        GTEST_SKIP() << "Skipping test calling warm_reset() on Galaxy configurations.";
    }

    uint32_t write_test_data = 0xDEADBEEF;

    auto chip_id = *cluster->get_target_device_ids().begin();
    auto tt_device = cluster->get_chip(chip_id)->get_tt_device();

    tt_device->bar_write32(
        tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
            tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset(),
        write_test_data);

    WarmReset::warm_reset();

    cluster.reset();

    cluster = std::make_unique<Cluster>();
    chip_id = *cluster->get_target_device_ids().begin();
    tt_device = cluster->get_chip(chip_id)->get_tt_device();

    auto read_test_data = tt_device->bar_read32(
        tt_device->get_architecture_implementation()->get_arc_axi_apb_peripheral_offset() +
        tt_device->get_architecture_implementation()->get_arc_reset_scratch_2_offset());

    EXPECT_NE(write_test_data, read_test_data);
}
