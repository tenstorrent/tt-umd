// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd::error {

struct UnsupportedCMFWData : public TTDeviceData {
    FirmwareBundleVersion found;
    FirmwareBundleVersion minimum;
};

struct UnsupportedCMFWError : public UmdError<UnsupportedCMFWData> {
    UnsupportedCMFWError(const TTDevice& tt_device, uint64_t topology_unique_id, FirmwareBundleVersion found);
};

struct CMFWMismatchData : public TTDeviceData {
    FirmwareBundleVersion expected;
    FirmwareBundleVersion found;
};

struct CMFWMismatchError : public UmdError<CMFWMismatchData> {
    CMFWMismatchError(
        const TTDevice& tt_device,
        uint64_t topology_unique_id,
        FirmwareBundleVersion expected,
        FirmwareBundleVersion found);
};

}  // namespace tt::umd::error
