// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <exception>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "tt-umd/firmware/erisc_firmware.hpp"
#include "tt-umd/firmware/firmware_utils.hpp"
#include "tt-umd/types/arch.hpp"
#include "tt-umd/utils/semver.hpp"

using namespace tt::umd;

TEST(Semver, Valid) {
    const std::map<std::string, SemVer> valid_test_cases = {
        {"1.29", SemVer(1, 29, 0)},      // technically invalid, but seen from TT-KMD
        {"1.28-bh2", SemVer(1, 28, 0)},  // technically invalid, but seen from TT-KMD
        {"0.0.4", SemVer(0, 0, 4)},
        {"1.2.3", SemVer(1, 2, 3)},
        {"10.20.30", SemVer(10, 20, 30)},
        {"1.1.2-prerelease+meta", SemVer(1, 1, 2)},
        {"1.1.2+meta", SemVer(1, 1, 2)},
        {"1.1.2+meta-valid", SemVer(1, 1, 2)},
        {"1.0.0-alpha", SemVer(1, 0, 0)},
        {"1.0.0-beta", SemVer(1, 0, 0)},
        {"1.0.0-alpha.beta", SemVer(1, 0, 0)},
        {"1.0.0-alpha.beta.1", SemVer(1, 0, 0)},
        {"1.0.0-alpha.1", SemVer(1, 0, 0)},
        {"1.0.0-alpha0.valid", SemVer(1, 0, 0)},
        {"1.0.0-alpha.0valid", SemVer(1, 0, 0)},
        {"1.0.0-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay", SemVer(1, 0, 0)},
        {"1.0.0-rc.1+build.1", SemVer(1, 0, 0, 1)},
        {"2.0.0-rc.1+build.123", SemVer(2, 0, 0, 1)},
        {"1.2.3-beta", SemVer(1, 2, 3)},
        {"10.2.3-DEV-SNAPSHOT", SemVer(10, 2, 3)},
        {"1.2.3-SNAPSHOT-123", SemVer(1, 2, 3)},
        {"1.0.0", SemVer(1, 0, 0)},
        {"2.0.0", SemVer(2, 0, 0)},
        {"1.1.7", SemVer(1, 1, 7)},
        {"2.0.0+build.1848", SemVer(2, 0, 0)},
        {"2.0.1-alpha.1227", SemVer(2, 0, 1)},
        {"1.0.0-alpha+beta", SemVer(1, 0, 0)},
        {"1.2.3----RC-SNAPSHOT.12.9.1--.12+788", SemVer(1, 2, 3)},
        {"1.2.3----R-S.12.9.1--.12+meta", SemVer(1, 2, 3)},
        {"1.2.3----RC-SNAPSHOT.12.9.1--.12", SemVer(1, 2, 3)},
        {"1.2.3-rc.1", SemVer(1, 2, 3, 1)},
        {"1.3.2-rc.255", SemVer(1, 3, 2, 255)},
        {"1.0.0-0A.is.legal", SemVer(1, 0, 0)}};

    for (const auto &[version_str, expected] : valid_test_cases) {
        SemVer actual(version_str);
        EXPECT_EQ(actual.major, expected.major);
        EXPECT_EQ(actual.minor, expected.minor);
        EXPECT_EQ(actual.patch, expected.patch);
        EXPECT_EQ(actual.pre_release, expected.pre_release);
    }
}

TEST(Semver, Invalid) {
    std::vector<std::string> invalid_test_cases = {
        "+invalid",
        "-invalid",
        "-invalid+invalid",
        "-invalid.01",
        "alpha",
        "alpha.beta",
        "alpha.beta.1",
        "alpha.1",
        "alpha+beta",
        "alpha_beta",
        "alpha.",
        "alpha..",
        "beta",
        "-alpha.",
        "+justmeta",
    };

    for (const auto &version_str : invalid_test_cases) {
        EXPECT_THROW(SemVer{version_str}, std::exception) << "'" << version_str << "' should be invalid";
    }
}
