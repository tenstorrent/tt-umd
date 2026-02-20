// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

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
class SemVer {
public:
    uint64_t major;
    uint64_t minor;
    uint64_t patch;
    uint64_t pre_release;

    constexpr SemVer() : major(0), minor(0), patch(0), pre_release(0) {}

    constexpr SemVer(uint64_t major, uint64_t minor, uint64_t patch, uint64_t pre_release = 00) :
        major(major), minor(minor), patch(patch), pre_release(pre_release) {}

    /*
     * Create a SemVer from a 32-bit integer by unpacking the following bits:
     * 0x00AABCCC where A is major, B is minor and C is patch.
     * Actual meaning of the tag is:
     * 0xEERRCDDD where E is entity, R is release, C is customer and D is debug.
     */
    static SemVer from_wormhole_eth_firmware_tag(std::uint32_t version) {
        uint64_t major = (version >> 16) & 0xFF;
        uint64_t minor = (version >> 12) & 0xF;
        uint64_t patch = version & 0xFFF;
        return SemVer(major, minor, patch);
    }

    SemVer(const std::string& version_str) : SemVer(parse(version_str)) {}

    std::string str() const {
        return (pre_release) ? fmt::format("{}.{}.{}-rc.{}", major, minor, patch, pre_release)
                             : fmt::format("{}.{}.{}", major, minor, patch);
    }

    bool operator<(const SemVer& other) const noexcept {
        uint64_t pr1 = (pre_release == 0) ? 256 : pre_release;
        uint64_t pr2 = (other.pre_release == 0) ? 256 : other.pre_release;
        return std::tie(major, minor, patch, pr1) < std::tie(other.major, other.minor, other.patch, pr2);
    }

    bool operator>(const SemVer& other) const { return other < *this; }

    bool operator==(const SemVer& other) const {
        return std::tie(major, minor, patch, pre_release) ==
               std::tie(other.major, other.minor, other.patch, other.pre_release);
    }

    bool operator!=(const SemVer& other) const { return !(*this == other); }

    bool operator<=(const SemVer& other) const { return !(other < *this); }

    bool operator>=(const SemVer& other) const { return !(*this < other); }

    std::string to_string() const { return str(); }

    /*
     * Compare two firmware bundle versions, treating major version 80 and above as legacy versions,
     * which are considered smaller than any non-legacy version.
     * @param v1 - first version to compare
     * @param v2 - second version to compare
     * @returns -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
     */
    static int compare_firmware_bundle(const SemVer& v1, const SemVer& v2) {
        auto normalize = [](const SemVer& v) {
            // Major version 80 is treated as legacy, so smaller than everything else.
            if (v.major >= 80) {
                return SemVer(0, v.minor, v.patch);
            }
            return SemVer(v.major, v.minor, v.patch);
        };

        auto v1_normalized = normalize(v1);
        auto v2_normalized = normalize(v2);

        return v1_normalized < v2_normalized ? -1 : (v1_normalized > v2_normalized ? 1 : 0);
    }

private:
    static SemVer parse(const std::string& version_str) {
        std::string version = version_str;
        size_t pos = version_str.find("-rc.");
        size_t count = 3;  // -rc length
        bool ispos = (pos != std::string::npos);
        if (ispos) {
            version.erase(pos, count);
        }
        std::istringstream iss(version);
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

                    if (std::getline(iss, token, '.') && ispos) {
                        pre_release = std::stoull(token);
                    }
                }
            }
        }
        return SemVer(major, minor, patch, pre_release);
    }
};

class FirmwareBundleVersion : public SemVer {
public:
    using SemVer::SemVer;

    static FirmwareBundleVersion from_firmware_bundle_tag(std::uint32_t tag) {
        uint64_t major = (tag >> 24) & 0xFF;
        uint64_t minor = (tag >> 16) & 0xFF;
        uint64_t patch = (tag >> 8) & 0xFF;
        uint64_t pre_release = tag & 0xFF;
        return FirmwareBundleVersion(major, minor, patch, pre_release);
    }

    bool operator<(const FirmwareBundleVersion& other) const noexcept {
        return SemVer::compare_firmware_bundle(*this, other) < 0;
    }

    bool operator>(const FirmwareBundleVersion& other) const noexcept {
        return SemVer::compare_firmware_bundle(*this, other) > 0;
    }

    bool operator==(const FirmwareBundleVersion& other) const noexcept {
        return SemVer::compare_firmware_bundle(*this, other) == 0;
    }

    bool operator!=(const FirmwareBundleVersion& other) const noexcept { return !(*this == other); }

    bool operator<=(const FirmwareBundleVersion& other) const noexcept { return !(*this > other); }

    bool operator>=(const FirmwareBundleVersion& other) const noexcept { return !(*this < other); }
};

// TODO: Remove after rename in tt-metal.
using semver_t = SemVer;

}  // namespace tt::umd

namespace std {
template <>
struct hash<tt::umd::SemVer> {
    std::size_t operator()(const tt::umd::SemVer& v) const noexcept {
        // Assumption: size_t is 64-bit.
        // Layout: [ Major (16) | Minor (16) | Patch (32) ].
        return (static_cast<size_t>(v.major) << 48) | (static_cast<size_t>(v.minor) << 32) |
               static_cast<size_t>(v.patch);
    }
};
}  // namespace std
