// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "device/mockup/tt_mockup_device.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/types/arch.h"

namespace test::mockup_device {

std::string get_env_arch_name() {
    constexpr std::string_view ARCH_NAME_ENV_VAR = "ARCH_NAME";
    if (const char *arch_name_ptr = std::getenv(ARCH_NAME_ENV_VAR.data())) {
        return arch_name_ptr;
    }
    throw std::runtime_error("Environment variable ARCH_NAME is not set.");
}

std::string get_soc_descriptor_file(tt::ARCH arch) {
    // const std::string umd_root = get_umd_root();

    switch (arch) {
        case tt::ARCH::GRAYSKULL:
            return test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml");
        case tt::ARCH::WORMHOLE_B0:
            return test_utils::GetAbsPath("tests/soc_descs/wormhole_b0_8x10.yaml");
        case tt::ARCH::BLACKHOLE:
            return test_utils::GetAbsPath("tests/soc_descs/blackhole_140_arch_type2.yaml");
        case tt::ARCH::Invalid:
            throw std::runtime_error("Invalid arch not supported");
        default:
            throw std::runtime_error("Unsupported device architecture");
    }
}

TEST(ApiMockupTest, CreateDevice) {
    const auto arch = tt::arch_from_str(get_env_arch_name());
    std::cout << "Creating mockup device" << std::endl;
    auto device_driver = std::make_unique<tt_MockupDevice>(get_soc_descriptor_file(arch));
}

}  // namespace test::mockup_device
