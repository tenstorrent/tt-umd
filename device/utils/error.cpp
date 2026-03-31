// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/error.hpp"

#include <fmt/format.h>

#include <cstdint>

#include "umd/device/utils/error_detail.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd::error {
using namespace tt::umd;

CMFWMismatchError::CMFWMismatchError(
    TTDevice& tt_device, uint64_t discovery_unique_id, const SemVer& expected, const SemVer& got) :
    UmdError<CMFWMismatchData>(
        fmt::format(
            "Firmware bundle version mismatch for device {}: expected {}, got {}",
            discovery_unique_id,
            expected.str(),
            got.str()),
        CMFWMismatchData{TTDeviceData{tt_device, discovery_unique_id}, expected, got}) {}

CMFWUnsupportedError::CMFWUnsupportedError(
    TTDevice& tt_device, uint64_t discovery_unique_id, const SemVer& got, const SemVer& minimum) :
    UmdError<CMFWUnsupportedData>(
        fmt::format(
            "Firmware bundle version {} on device {} is older than the minimum compatible version {} for {} "
            "architecture.",
            got.str(),
            discovery_unique_id,
            minimum.str(),
            arch_to_str(tt_device.get_arch())),
        CMFWUnsupportedData{TTDeviceData{tt_device, discovery_unique_id}, got, minimum}) {}

}  // namespace tt::umd::error
