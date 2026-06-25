// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/topology/topology_discovery_error.hpp"

#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd::error {
UnsupportedCMFWError::UnsupportedCMFWError(
    const TTDevice& tt_device,
    uint64_t topology_unique_id,
    FirmwareBundleVersion found,
    FirmwareBundleVersion minimum) :
    UmdError<UnsupportedCMFWData>(
        fmt::format(
            "Firmware bundle version {} on device {} is older than the minimum compatible version {} for {} "
            "architecture.",
            found.to_string(),
            topology_unique_id,
            minimum.to_string(),
            arch_to_str(tt_device.get_arch())),
        {{tt_device, topology_unique_id}, found, minimum}) {}

CMFWMismatchError::CMFWMismatchError(
    const TTDevice& tt_device,
    uint64_t topology_unique_id,
    FirmwareBundleVersion expected,
    FirmwareBundleVersion found) :
    UmdError<CMFWMismatchData>(
        fmt::format(
            "Firmware bundle version mismatch for device {}: expected {}, got {}.",
            topology_unique_id,
            expected.to_string(),
            found.to_string()),
        {{tt_device, topology_unique_id}, expected, found}) {}

}  // namespace tt::umd::error
