// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "common/open_device_or_skip.hpp"

using tt::umd::open_device_or_skip;

namespace {

// Device 1 stands in for a chip the KMD refuses to map; every other device opens as its own id.
const std::function<int(int)> open_all_but_device_1 = [](int device_id) {
    if (device_id == 1) {
        throw std::runtime_error("Query mappings failed on device 1");
    }
    return device_id;
};

}  // namespace

TEST(OpenDeviceOrSkip, FailedOpenThrowsWithoutSkip) {
    std::map<int, std::string> failed_opens;
    EXPECT_THROW(open_device_or_skip(1, false, open_all_but_device_1, failed_opens), std::runtime_error);
    EXPECT_TRUE(failed_opens.empty());
}

TEST(OpenDeviceOrSkip, FailedOpenIsRecordedWithSkip) {
    std::map<int, std::string> failed_opens;
    EXPECT_EQ(open_device_or_skip(1, true, open_all_but_device_1, failed_opens), std::nullopt);
    EXPECT_EQ(failed_opens, (std::map<int, std::string>{{1, "Query mappings failed on device 1"}}));
}

TEST(OpenDeviceOrSkip, OtherDevicesOpenAfterASkippedOne) {
    std::map<int, std::string> failed_opens;
    EXPECT_EQ(open_device_or_skip(0, true, open_all_but_device_1, failed_opens), std::optional<int>(0));
    EXPECT_EQ(open_device_or_skip(1, true, open_all_but_device_1, failed_opens), std::nullopt);
    EXPECT_EQ(open_device_or_skip(2, true, open_all_but_device_1, failed_opens), std::optional<int>(2));
    EXPECT_EQ(failed_opens.size(), 1);
    EXPECT_EQ(failed_opens.count(1), 1);
}
