// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>

#include "tt-umd-utils/semver.hpp"

namespace tt::umd {

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
        return compare_firmware_bundle(*this, other) < 0;
    }

    bool operator>(const FirmwareBundleVersion& other) const noexcept {
        return compare_firmware_bundle(*this, other) > 0;
    }

    bool operator==(const FirmwareBundleVersion& other) const noexcept {
        return compare_firmware_bundle(*this, other) == 0;
    }

    bool operator!=(const FirmwareBundleVersion& other) const noexcept { return !(*this == other); }

    bool operator<=(const FirmwareBundleVersion& other) const noexcept { return !(*this > other); }

    bool operator>=(const FirmwareBundleVersion& other) const noexcept { return !(*this < other); }

private:
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
};

}  // namespace tt::umd

namespace std {
template <>
struct hash<tt::umd::FirmwareBundleVersion> {
    std::size_t operator()(const tt::umd::FirmwareBundleVersion& v) const noexcept {
        return std::hash<tt::umd::SemVer>{}(v);
    }
};
}  // namespace std
