/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gtest/gtest.h>

#include <map>

#include "umd/device/firmware/erisc_firmware.hpp"
#include "umd/device/firmware/firmware_utils.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/utils/semver.hpp"

using namespace tt::umd;

TEST(Semver, Valid) {
    const std::map<std::string, semver_t> valid_test_cases = {
        {"1.29", semver_t(1, 29, 0)},      // technically invalid, but seen from TT-KMD
        {"1.28-bh2", semver_t(1, 28, 0)},  // technically invalid, but seen from TT-KMD
        {"0.0.4", semver_t(0, 0, 4)},
        {"1.2.3", semver_t(1, 2, 3)},
        {"10.20.30", semver_t(10, 20, 30)},
        {"1.1.2-prerelease+meta", semver_t(1, 1, 2)},
        {"1.1.2+meta", semver_t(1, 1, 2)},
        {"1.1.2+meta-valid", semver_t(1, 1, 2)},
        {"1.0.0-alpha", semver_t(1, 0, 0)},
        {"1.0.0-beta", semver_t(1, 0, 0)},
        {"1.0.0-alpha.beta", semver_t(1, 0, 0)},
        {"1.0.0-alpha.beta.1", semver_t(1, 0, 0)},
        {"1.0.0-alpha.1", semver_t(1, 0, 0)},
        {"1.0.0-alpha0.valid", semver_t(1, 0, 0)},
        {"1.0.0-alpha.0valid", semver_t(1, 0, 0)},
        {"1.0.0-alpha-a.b-c-somethinglong+build.1-aef.1-its-okay", semver_t(1, 0, 0)},
        {"1.0.0-rc.1+build.1", semver_t(1, 0, 0, 1)},
        {"2.0.0-rc.1+build.123", semver_t(2, 0, 0, 1)},
        {"1.2.3-beta", semver_t(1, 2, 3)},
        {"10.2.3-DEV-SNAPSHOT", semver_t(10, 2, 3)},
        {"1.2.3-SNAPSHOT-123", semver_t(1, 2, 3)},
        {"1.0.0", semver_t(1, 0, 0)},
        {"2.0.0", semver_t(2, 0, 0)},
        {"1.1.7", semver_t(1, 1, 7)},
        {"2.0.0+build.1848", semver_t(2, 0, 0)},
        {"2.0.1-alpha.1227", semver_t(2, 0, 1)},
        {"1.0.0-alpha+beta", semver_t(1, 0, 0)},
        {"1.2.3----RC-SNAPSHOT.12.9.1--.12+788", semver_t(1, 2, 3)},
        {"1.2.3----R-S.12.9.1--.12+meta", semver_t(1, 2, 3)},
        {"1.2.3----RC-SNAPSHOT.12.9.1--.12", semver_t(1, 2, 3)},
        {"1.2.3-rc.1", semver_t(1, 2, 3, 1)},
        {"1.3.2-rc.255", semver_t(1, 3, 2, 255)},
        {"1.0.0-0A.is.legal", semver_t(1, 0, 0)}};

    for (const auto &[version_str, expected] : valid_test_cases) {
        semver_t actual(version_str);
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
        EXPECT_THROW(semver_t{version_str}, std::exception) << "'" << version_str << "' should be invalid";
    }
}

TEST(Semver, FirmwareExpectedVersions) {
    auto f = get_expected_eth_firmware_version_from_firmware_bundle;
    EXPECT_EQ(std::nullopt, f(semver_t(80, 0, 0), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(semver_t(6, 14, 0), f(semver_t(80, 17, 0), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(semver_t(6, 14, 0), f(semver_t(80, 18, 0), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(semver_t(6, 14, 0), f(semver_t(18, 0, 0), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(semver_t(6, 15, 0), f(semver_t(18, 4, 0), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(semver_t(6, 15, 0), f(semver_t(18, 4, 1), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(semver_t(7, 0, 0), f(semver_t(18, 6, 0), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(erisc_firmware::WH_ERISC_FW_VERSION_MAP.back().second, f(semver_t(79, 99, 99), tt::ARCH::WORMHOLE_B0));
    EXPECT_EQ(std::nullopt, f(semver_t(18, 0, 0), tt::ARCH::BLACKHOLE));
    EXPECT_EQ(semver_t(1, 6, 0), f(semver_t(18, 11, 0), tt::ARCH::BLACKHOLE));
    EXPECT_EQ(erisc_firmware::BH_ERISC_FW_VERSION_MAP.back().second, f(semver_t(79, 99, 99), tt::ARCH::BLACKHOLE));
}
