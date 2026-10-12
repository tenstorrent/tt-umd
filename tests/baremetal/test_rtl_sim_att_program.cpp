// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "simulation/rtl_sim_att_program.hpp"
#include "tests/test_utils/fetch_local_files.hpp"

using namespace tt::umd;
using ::testing::HasSubstr;

namespace {

constexpr IpDeviceId DEV0 = static_cast<IpDeviceId>(0);
constexpr IpDeviceId DEV1 = static_cast<IpDeviceId>(1);
constexpr IpDeviceId DEV2 = static_cast<IpDeviceId>(2);

// A build directory under tests/att_programs holding one att_program.yaml.
std::filesystem::path program_dir(const std::string& name) { return test_utils::GetAbsPath("att_programs/" + name); }

// The message loading @p name throws, or "" if it doesn't throw.
std::string error_of(const std::string& name) {
    try {
        RtlSimAttProgram::load(program_dir(name));
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST(RtlSimAttProgram, AbsentFileIsNoProgram) { EXPECT_FALSE(RtlSimAttProgram::load(program_dir("")).has_value()); }

TEST(RtlSimAttProgram, ReadsWritesPerDeviceInOrder) {
    const std::optional<RtlSimAttProgram> program = RtlSimAttProgram::load(program_dir("two_devices"));
    ASSERT_TRUE(program.has_value());

    const auto& dev1 = program->get_writes(DEV1);
    ASSERT_EQ(dev1.size(), 3u);
    EXPECT_EQ(dev1[0].core, tt_xy_pair(1, 0));
    EXPECT_EQ(dev1[0].address, 0x2010000u);
    EXPECT_EQ(dev1[0].value, 0u);
    EXPECT_EQ(dev1[1].address, 0x2012410u);
    EXPECT_EQ(dev1[1].value, 1u);
    // A bridge register over APB, bit 56 set and sent to core (0, 0): 0x0100'0000'0201'0000.
    EXPECT_EQ(dev1[2].core, tt_xy_pair(0, 0));
    EXPECT_EQ(dev1[2].address, (uint64_t{1} << 56) | 0x2010000u);

    const auto& dev0 = program->get_writes(DEV0);
    ASSERT_EQ(dev0.size(), 1u);
    EXPECT_EQ(dev0[0].core, tt_xy_pair(0, 1));
    EXPECT_EQ(dev0[0].value, 0x40u);
}

TEST(RtlSimAttProgram, UnlistedDeviceHasNoWrites) {
    EXPECT_TRUE(RtlSimAttProgram::load(program_dir("two_devices"))->get_writes(DEV2).empty());
}

TEST(RtlSimAttProgram, RejectsDuplicateDevice) {
    EXPECT_THAT(error_of("duplicate_device"), HasSubstr("lists device 0 twice."));
}

TEST(RtlSimAttProgram, RejectsMissingDevices) {
    EXPECT_THAT(error_of("missing_devices"), HasSubstr("`devices` must be a list."));
}

TEST(RtlSimAttProgram, RejectsDeviceWithoutWrites) {
    EXPECT_THAT(error_of("missing_writes"), HasSubstr("device 0 has no `writes` list."));
}

TEST(RtlSimAttProgram, RejectsMalformedWrite) {
    EXPECT_THAT(error_of("malformed_write"), HasSubstr("a write is [x, y, address, value]."));
}
