// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <map>
#include <vector>

#include "umd/device/topology/ip_layout.hpp"

using namespace tt::umd;

namespace {

class FakeIpLayout : public IpLayout {
public:
    FakeIpLayout(size_t num_access_points, const std::vector<std::vector<uint32_t>>& devices) :
        num_access_points_(num_access_points) {
        for (uint32_t device = 0; device < devices.size(); device++) {
            std::vector<AccessPointId>& ids = devices_[static_cast<IpDeviceId>(device)];
            for (uint32_t id : devices[device]) {
                ids.push_back(static_cast<AccessPointId>(id));
            }
        }
    }

    size_t get_num_access_points() const override { return num_access_points_; }

    std::map<IpDeviceId, std::vector<AccessPointId>> get_devices() const override { return devices_; }

    AccessPointLocation get_location(AccessPointId) const override { return {}; }

private:
    size_t num_access_points_;
    std::map<IpDeviceId, std::vector<AccessPointId>> devices_;
};

}  // namespace

TEST(IpLayout, OneDeviceTwoAccessPoints) { EXPECT_NO_THROW(validate_ip_layout(FakeIpLayout(2, {{0, 1}}))); }

TEST(IpLayout, TwoDevicesOneAccessPointEach) { EXPECT_NO_THROW(validate_ip_layout(FakeIpLayout(2, {{0}, {1}}))); }

TEST(IpLayout, NoDevices) { EXPECT_THROW(validate_ip_layout(FakeIpLayout(1, {})), std::exception); }

TEST(IpLayout, DeviceWithNoAccessPoints) {
    EXPECT_THROW(validate_ip_layout(FakeIpLayout(1, {{0}, {}})), std::exception);
}

TEST(IpLayout, AccessPointListedTwiceByOneDevice) {
    EXPECT_THROW(validate_ip_layout(FakeIpLayout(1, {{0, 0}})), std::exception);
}

TEST(IpLayout, UnknownAccessPoint) { EXPECT_THROW(validate_ip_layout(FakeIpLayout(1, {{0, 1}})), std::exception); }

TEST(IpLayout, AccessPointOwnedByTwoDevices) {
    EXPECT_THROW(validate_ip_layout(FakeIpLayout(1, {{0}, {0}})), std::exception);
}

TEST(IpLayout, UnownedAccessPoint) { EXPECT_THROW(validate_ip_layout(FakeIpLayout(2, {{0}})), std::exception); }
