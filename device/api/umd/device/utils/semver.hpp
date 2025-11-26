/*
 * SPDX-FileCopyrightText: (c) 2024 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <tuple>

namespace tt::umd {

/**
 * Based on Semantic Versioning 2.0.0 (https://semver.org/) but more permissive.
 * TT-KMD reports version strings that are technically not semver compliant.
 */
class semver_t {
public:
    uint64_t major;
    uint64_t minor;
    uint64_t patch;
    uint64_t pre_release;

    constexpr semver_t() : major(0), minor(0), patch(0), pre_release(0) {}

    constexpr semver_t(std::uint32_t version) :
        major((version >> 16) & 0xff), minor((version >> 12) & 0xf), patch(version & 0xfff), pre_release(0) {}

    constexpr semver_t(uint64_t major, uint64_t minor, uint64_t patch, uint64_t pre_release = 0) :
        major(major), minor(minor), patch(patch), pre_release(pre_release) {}

    static semver_t from_firmware_bundle_tag(std::uint32_t version) {
        uint64_t major = (version >> 24) & 0xFF;
        uint64_t minor = (version >> 16) & 0xFF;
        uint64_t patch = (version >> 8) & 0xFF;
        uint64_t pre_release = version & 0xFF;
        return semver_t(major, minor, patch, pre_release);
    }

    static semver_t from_wormhole_eth_firmware_tag(std::uint32_t version) {
        uint64_t major = (version >> 16) & 0xff;
        uint64_t minor = (version >> 12) & 0xf;
        uint64_t patch = version & 0xfff;
        return semver_t(major, minor, patch);
    }

    /*
     * Create a semver_t from a 32-bit integer by unpacking the following bits:
     * 0x00AABCCC where A is major, B is minor and C is patch.
     */
    static semver_t from_eth_fw_tag(uint32_t version) {
        return semver_t((version >> 16) & 0xFF, (version >> 12) & 0xF, version & 0xFFF);
    }

    semver_t(const std::string& version_str) : semver_t(parse(version_str)) {}

    std::string str() const {
        return (pre_release) ? fmt::format("{}.{}.{}-rc.{}", major, minor, patch, pre_release)
                             : fmt::format("{}.{}.{}-rc.{}", major, minor, patch, pre_release);
    }

    bool operator<(const semver_t& other) const noexcept {
        uint64_t pr1 = (pre_release == 0) ? 256 : pre_release;
        uint64_t pr2 = (other.pre_release == 0) ? 256 : other.pre_release;
        return std::tie(major, minor, patch, pr1) < std::tie(other.major, other.minor, other.patch, pr2);
    }

    bool operator>(const semver_t& other) const { return other < *this; }

    bool operator==(const semver_t& other) const {
        return std::tie(major, minor, patch, pre_release) ==
               std::tie(other.major, other.minor, other.patch, other.pre_release);
    }

    bool operator!=(const semver_t& other) const { return !(*this == other); }

    bool operator<=(const semver_t& other) const { return !(other < *this); }

    bool operator>=(const semver_t& other) const { return !(*this < other); }

    std::string to_string() const {
        return (pre_release) ? fmt::format("{}.{}.{}-rc.{}", major, minor, patch, pre_release)
                             : fmt::format("{}.{}.{}-rc.{}", major, minor, patch, pre_release);
    }

    /*
     * Compare two firmware bundle versions, treating major version 80 and above as legacy versions,
     * which are considered smaller than any non-legacy version.
     * @param v1 - first version to compare
     * @param v2 - second version to compare
     * @returns -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
     */
    static int compare_firmware_bundle(const semver_t& v1, const semver_t& v2) {
        auto normalize = [](const semver_t& v) {
            // Major version 80 is treated as legacy, so smaller than everything else.
            if (v.major >= 80) {
                return std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>(0, v.minor, v.patch, v.pre_release);
            }
            return std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>(v.major, v.minor, v.patch, v.pre_release);
        };

        auto v1_normalized = normalize(v1);
        auto v2_normalized = normalize(v2);

        return v1_normalized < v2_normalized ? -1 : (v1_normalized > v2_normalized ? 1 : 0);
    }

private:
    static semver_t parse(const std::string& version_str) {
        std::istringstream iss(version_str);
        std::string token;
        uint64_t major = 0;
        uint64_t minor = 0;
        uint64_t patch = 0;
        uint64_t pre_release = 0;

        if (std::getline(iss, token, '.')) {
            major = std::stoull(token);

            if (std::getline(iss, token, '.')) {
                minor = std::stoull(token);

                if (std::getline(iss, token, '.')) {
                    patch = std::stoull(token);

                    if (std::getline(iss, token, '-') && pre_release != 0) {
                        pre_release = std::stoull(token);
                    }
                }
            }
        }
        return semver_t(major, minor, patch, pre_release);
    }
};

}  // namespace tt::umd
