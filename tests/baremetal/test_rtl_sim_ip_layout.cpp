// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "simulation/rtl_sim_ip_layout.hpp"
#include "tests/test_utils/fetch_local_files.hpp"

using namespace tt::umd;
using ::testing::HasSubstr;

namespace {

constexpr AccessPointId AP0 = static_cast<AccessPointId>(0);
constexpr AccessPointId AP1 = static_cast<AccessPointId>(1);
constexpr IpDeviceId DEV0 = static_cast<IpDeviceId>(0);
constexpr IpDeviceId DEV1 = static_cast<IpDeviceId>(1);

using Devices = std::map<IpDeviceId, std::vector<AccessPointId>>;

// A build directory under tests/ip_layouts holding one ip_layout.yaml.
std::filesystem::path layout_dir(const std::string& name) { return test_utils::GetAbsPath("ip_layouts/" + name); }

std::filesystem::path soc_descriptor_of(const std::string& name) {
    return layout_dir(name) / "../../soc_descs/quasar_simulation_1x3.yaml";
}

// The message RtlSimIpLayout throws for @p name, or "" if it doesn't throw.
std::string error_of(const std::string& name) {
    try {
        RtlSimIpLayout layout(layout_dir(name));
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST(RtlSimIpLayout, OneDeviceTwoAccessPoints) {
    const RtlSimIpLayout layout(layout_dir("one_device_two_access_points"));

    EXPECT_EQ(layout.get_num_access_points(), 2u);
    EXPECT_EQ(layout.get_devices(), (Devices{{DEV0, {AP0, AP1}}}));
    EXPECT_EQ(layout.get_host_access(AP0), "col0");
    EXPECT_EQ(layout.get_host_access(AP1), "col1");
    EXPECT_EQ(layout.get_soc_descriptor(DEV0), soc_descriptor_of("one_device_two_access_points"));

    EXPECT_EQ(layout.get_location(AP0).coord, tt_xy_pair(0, 2));
    EXPECT_EQ(layout.get_location(AP0).flat_address, 0x0800000000000ULL);
    EXPECT_EQ(layout.get_location(AP1).coord, tt_xy_pair(1, 2));
    EXPECT_FALSE(layout.get_location(AP1).flat_address.has_value());
}

TEST(RtlSimIpLayout, TwoDevicesOneAccessPointEach) {
    const RtlSimIpLayout layout(layout_dir("two_devices_one_access_point_each"));

    EXPECT_EQ(layout.get_devices(), (Devices{{DEV0, {AP0}}, {DEV1, {AP1}}}));
    EXPECT_EQ(layout.get_soc_descriptor(DEV1), soc_descriptor_of("two_devices_one_access_point_each"));
    EXPECT_FALSE(layout.get_location(AP0).coord.has_value());
    EXPECT_FALSE(layout.get_location(AP0).flat_address.has_value());
}

TEST(RtlSimIpLayout, ExplicitDeviceIds) {
    const RtlSimIpLayout layout(layout_dir("explicit_device_ids"));

    EXPECT_EQ(layout.get_devices(), (Devices{{DEV0, {AP1}}, {DEV1, {AP0}}}));
}

TEST(RtlSimIpLayout, MissingFile) { EXPECT_THAT(error_of(""), HasSubstr("ip_layout.yaml not found.")); }

TEST(RtlSimIpLayout, DuplicateDeviceId) {
    EXPECT_THAT(error_of("duplicate_device_id"), HasSubstr("lists device 0 twice."));
}

TEST(RtlSimIpLayout, MixedExplicitAndPositionalDeviceIds) {
    EXPECT_THAT(error_of("mixed_device_ids"), HasSubstr("give every device an id, or none."));
}

TEST(RtlSimIpLayout, SparseDeviceIds) {
    EXPECT_THAT(error_of("sparse_device_ids"), HasSubstr("IP layout device ids must be 0..1."));
}

TEST(RtlSimIpLayout, MissingHostAccess) {
    EXPECT_THAT(error_of("missing_host_access"), HasSubstr("access point 1 has no host_access."));
}

TEST(RtlSimIpLayout, MissingSocDescriptor) {
    EXPECT_THAT(error_of("missing_soc_descriptor"), HasSubstr("device 0 has no soc_descriptor."));
}

TEST(RtlSimIpLayout, SocDescriptorNotFound) {
    EXPECT_THAT(error_of("soc_descriptor_not_found"), HasSubstr("no_such_soc_descriptor.yaml not found."));
}

TEST(RtlSimIpLayout, UnownedAccessPoint) {
    EXPECT_THAT(error_of("unowned_access_point"), HasSubstr("access point 1 is owned by 0 devices, expected 1."));
}

TEST(RtlSimIpLayout, AccessPointListedTwiceByOneDevice) {
    EXPECT_THAT(error_of("access_point_listed_twice"), HasSubstr("device 0 lists access point 0 twice."));
}
