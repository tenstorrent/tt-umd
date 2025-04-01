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

TEST(ApiMockupTest, CreateDevice) {
    std::cout << "Creating mockup device" << std::endl;
    for (auto soc_descriptor_file :
         {"tests/soc_descs/wormhole_b0_8x10.yaml", "tests/soc_descs/blackhole_140_arch.yaml"}) {
        auto device_driver = std::make_unique<tt_MockupDevice>(test_utils::GetAbsPath(soc_descriptor_file));
    }
}

}  // namespace test::mockup_device
