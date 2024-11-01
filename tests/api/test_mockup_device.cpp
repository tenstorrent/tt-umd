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
#include "device/tt_arch_types.h"

namespace test::mockup_device {

std::string get_umd_root() {
    for (auto path = std::filesystem::current_path(); !path.empty(); path = path.parent_path()) {
        if (path.filename() == "build") {
            return path.parent_path().string();
        }
    }
    throw std::runtime_error("Build folder not found in the path.");
}

std::string get_env_arch_name() {
    constexpr std::string_view ARCH_NAME_ENV_VAR = "ARCH_NAME";
    if (const char *arch_name_ptr = std::getenv(ARCH_NAME_ENV_VAR.data())) {
        return arch_name_ptr;
    }
    throw std::runtime_error("Environment variable ARCH_NAME is not set.");
}

tt::ARCH get_arch_from_string(const std::string &arch_str) {
    if (arch_str == "grayskull" || arch_str == "GRAYSKULL")
        return tt::ARCH::GRAYSKULL;
    if (arch_str == "wormhole" || arch_str == "WORMHOLE")
        return tt::ARCH::WORMHOLE;
    if (arch_str == "wormhole_b0" || arch_str == "WORMHOLE_B0")
        return tt::ARCH::WORMHOLE_B0;
    if (arch_str == "blackhole" || arch_str == "BLACKHOLE")
        return tt::ARCH::BLACKHOLE;
    if (arch_str == "Invalid" || arch_str == "INVALID")
        return tt::ARCH::Invalid;

    throw std::runtime_error(arch_str + " is not recognized as tt::ARCH.");
}

std::string get_soc_description_file(tt::ARCH arch) {
    const std::string umd_root = get_umd_root();

    switch (arch) {
        case tt::ARCH::GRAYSKULL: return umd_root + "/tests/soc_descs/grayskull_10x12.yaml";
        case tt::ARCH::WORMHOLE_B0: return umd_root + "/tests/soc_descs/wormhole_b0_8x10.yaml";
        case tt::ARCH::BLACKHOLE: return umd_root + "/tests/soc_descs/blackhole_140_arch.yaml";
        case tt::ARCH::WORMHOLE: throw std::runtime_error("WORMHOLE arch not supported");
        case tt::ARCH::Invalid: throw std::runtime_error("Invalid arch not supported");
        default: throw std::runtime_error("Unsupported device architecture");
    }
}

TEST(ApiMockupTest, CreateDevice) {
    const auto arch = get_arch_from_string(get_env_arch_name());
    auto device_driver = std::make_unique<tt_MockupDevice>(get_soc_description_file(arch));
}

}  // namespace test::mockup_device