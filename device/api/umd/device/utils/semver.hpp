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

    semver_t(uint64_t major, uint64_t minor, uint64_t patch) {
        this->major = major;
        this->minor = minor;
        this->patch = patch;
    }

    semver_t(const std::string& version_str) : semver_t(parse(version_str)) {}

    bool operator<(const semver_t& other) const {
        return std::tie(major, minor, patch) < std::tie(other.major, other.minor, other.patch);
    }

    bool operator>(const semver_t& other) const { return other < *this; }

    bool operator==(const semver_t& other) const {
        return std::tie(major, minor, patch) == std::tie(other.major, other.minor, other.patch);
    }

    bool operator!=(const semver_t& other) const { return !(*this == other); }

    bool operator<=(const semver_t& other) const { return !(other < *this); }

    bool operator>=(const semver_t& other) const { return !(*this < other); }

    std::string to_string() const { return fmt::format("{}.{}.{}", major, minor, patch); }

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
                return std::tuple<uint64_t, uint64_t, uint64_t>(0, v.minor, v.patch);
            }
            return std::tuple<uint64_t, uint64_t, uint64_t>(v.major, v.minor, v.patch);
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

        if (std::getline(iss, token, '.')) {
            major = std::stoull(token);

            if (std::getline(iss, token, '.')) {
                minor = std::stoull(token);

                if (std::getline(iss, token, '.')) {
                    patch = std::stoull(token);
                }
            }
        }
        return semver_t(major, minor, patch);
    }
};

}  // namespace tt::umd
