// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery_error.hpp"

#include <fmt/format.h>

#include <string>

#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd::error {

namespace {

std::string describe_device(const TTDeviceData& data) {
    return fmt::format(
        "{} device {} ({}, unique id {:#x})",
        DeviceTypeToString.at(data.io_device_type),
        data.chip_id,
        arch_to_str(data.arch),
        data.discovery_unique_id.value_or(0));
}

DeviceCoreData make_core_data(const TTDevice& tt_device, uint64_t topology_unique_id, CoreCoord core, NocId noc_id) {
    DeviceCoreData data;
    static_cast<TTDeviceData&>(data) = TTDeviceData(tt_device, topology_unique_id);
    data.core = core;
    data.noc_id = noc_id;
    return data;
}

}  // namespace

UnsupportedCMFWError::UnsupportedCMFWError(
    const TTDevice& tt_device,
    uint64_t topology_unique_id,
    FirmwareBundleVersion found,
    FirmwareBundleVersion minimum) :
    UmdError<UnsupportedCMFWData>(
        "", UnsupportedCMFWData{TTDeviceData(tt_device, topology_unique_id), found, minimum}) {
    message() = fmt::format(
        "Firmware bundle {} on {} is older than the minimum supported {}.",
        found.to_string(),
        describe_device(data()),
        minimum.to_string());
}

CMFWMismatchError::CMFWMismatchError(
    const TTDevice& tt_device,
    uint64_t topology_unique_id,
    FirmwareBundleVersion expected,
    FirmwareBundleVersion found) :
    UmdError<CMFWMismatchData>("", CMFWMismatchData{TTDeviceData(tt_device, topology_unique_id), expected, found}) {
    message() = fmt::format(
        "Firmware bundle {} on {} differs from {} found on the other devices of the cluster.",
        found.to_string(),
        describe_device(data()),
        expected.to_string());
}

EthFirmwareMismatchError::EthFirmwareMismatchError(
    const TTDevice& tt_device,
    uint64_t topology_unique_id,
    SemVer expected,
    SemVer found,
    CoreCoord core,
    NocId noc_id) :
    UmdError<EthFirmwareMismatchData>(
        "", EthFirmwareMismatchData{make_core_data(tt_device, topology_unique_id, core, noc_id), expected, found}) {
    message() = fmt::format(
        "ETH firmware {} on core {} of {} differs from the expected {}.",
        found.to_string(),
        core.str(),
        describe_device(data()),
        expected.to_string());
}

UnexpectedRoutingFirmwareConfigError::UnexpectedRoutingFirmwareConfigError(
    const TTDevice& tt_device, uint64_t topology_unique_id, bool expected, bool found, CoreCoord core, NocId noc_id) :
    UmdError<UnexpectedRoutingFirmwareConfigData>(
        "",
        UnexpectedRoutingFirmwareConfigData{
            make_core_data(tt_device, topology_unique_id, core, noc_id), expected, found}) {
    message() = fmt::format(
        "Routing firmware on core {} of {} is {}, expected it {}.",
        core.str(),
        describe_device(data()),
        found ? "enabled" : "disabled",
        expected ? "enabled" : "disabled");
}

EthFirmwareHeartbeatError::EthFirmwareHeartbeatError(
    const TTDevice& tt_device, uint64_t topology_unique_id, uint32_t heartbeat_value, CoreCoord core, NocId noc_id) :
    UmdError<EthFirmwareHeartbeatData>(
        "", EthFirmwareHeartbeatData{make_core_data(tt_device, topology_unique_id, core, noc_id), heartbeat_value}) {
    message() = fmt::format(
        "ETH firmware heartbeat on core {} of {} is not advancing (last value {:#x}).",
        core.str(),
        describe_device(data()),
        heartbeat_value);
}

}  // namespace tt::umd::error
