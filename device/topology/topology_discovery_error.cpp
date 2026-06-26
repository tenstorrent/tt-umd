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

EthFirmwareMismatchError::EthFirmwareMismatchError(
    const TTDevice& tt_device, uint64_t topology_unique_id, SemVer expected, SemVer found, xy_pair core, NocId noc_id) :
    UmdError<EthFirmwareMismatchData>(
        fmt::format(
            "ETH FW version mismatch for device ASIC ID: {} ETH core {}, expected: {}, found {}.",
            topology_unique_id,
            core.str(),
            expected.to_string(),
            found.to_string()),
        {{{tt_device, topology_unique_id}, core, noc_id}, expected, found}) {}

UnexpectedRoutingFirmwareConfigError::UnexpectedRoutingFirmwareConfigError(
    const TTDevice& tt_device, uint64_t topology_unique_id, bool expected, bool found, xy_pair core, NocId noc_id) :
    UmdError<UnexpectedRoutingFirmwareConfigData>(
        fmt::format(
            "Routing firmware for device ASIC ID: {} ETH core {} is unexpectedly {}.",
            topology_unique_id,
            core.str(),
            found ? "enabled" : "disabled"),
        {{{tt_device, topology_unique_id}, core, noc_id}, expected, found}) {}

EthFirmwareHeartbeatError::EthFirmwareHeartbeatError(
    const TTDevice& tt_device, uint64_t topology_unique_id, uint32_t heartbeat_value, xy_pair core, NocId noc_id) :
    UmdError<EthFirmwareHeartbeatData>(
        fmt::format(
            "Timed out waiting for ETH heartbeat on device ASIC ID: {}, ETH core {} to {}. Stuck at {:#x}",
            topology_unique_id,
            core.str(),
            heartbeat_value == 0 ? "start" : "advance",
            heartbeat_value),
        {{{tt_device, topology_unique_id}, core, noc_id}, heartbeat_value}) {}

}  // namespace tt::umd::error
