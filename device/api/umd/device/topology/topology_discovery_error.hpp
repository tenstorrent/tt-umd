// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd::error {

struct UnsupportedCMFWData : public TTDeviceData {
    FirmwareBundleVersion found;
    FirmwareBundleVersion minimum;
};

struct UnsupportedCMFWError : public UmdError<UnsupportedCMFWData> {
    UnsupportedCMFWError(
        const TTDevice& tt_device,
        uint64_t topology_unique_id,
        FirmwareBundleVersion found,
        FirmwareBundleVersion minimum);
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

struct EthFirmwareMismatchData : public DeviceCoreData {
    SemVer expected;
    SemVer found;
};

struct EthFirmwareMismatchError : public UmdError<EthFirmwareMismatchData> {
    EthFirmwareMismatchError(
        const TTDevice& tt_device,
        uint64_t topology_unique_id,
        SemVer expected,
        SemVer found,
        CoreCoord core,
        NocId noc_id = NocId::DEFAULT_NOC);
};

struct UnexpectedRoutingFirmwareConfigData : public DeviceCoreData {
    bool expected;
    bool found;
};

struct UnexpectedRoutingFirmwareConfigError : public UmdError<UnexpectedRoutingFirmwareConfigData> {
    UnexpectedRoutingFirmwareConfigError(
        const TTDevice& tt_device,
        uint64_t topology_unique_id,
        bool expected,
        bool found,
        CoreCoord core,
        NocId noc_id = NocId::DEFAULT_NOC);
};

struct EthFirmwareHeartbeatData : public DeviceCoreData {
    uint32_t value;
};

struct EthFirmwareHeartbeatError : public UmdError<EthFirmwareHeartbeatData> {
    EthFirmwareHeartbeatError(
        const TTDevice& tt_device,
        uint64_t topology_unique_id,
        uint32_t heartbeat_value,
        CoreCoord core,
        NocId noc_id = NocId::DEFAULT_NOC);
};

}  // namespace tt::umd::error
