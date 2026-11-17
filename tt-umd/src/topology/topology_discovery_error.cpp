// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/topology/topology_discovery_error.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <string>

#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd::error {

namespace {

// Topology discovery names a device by the unique id it gave it, so that is what a message leads with.
std::string describe_device(const TTDevice &tt_device, uint64_t topology_unique_id) {
    return fmt::format(
        "device 0x{:x} ({} device {})",
        topology_unique_id,
        DeviceTypeToString.at(tt_device.get_communication_device_type()),
        tt_device.get_communication_device_id());
}

template <typename DATA_T>
DATA_T make_core_data(const TTDevice &tt_device, uint64_t topology_unique_id, CoreCoord core, NocId noc_id) {
    DATA_T data;
    static_cast<TTDeviceData &>(data) = TTDeviceData(tt_device, topology_unique_id);
    data.core = core;
    data.noc_id = noc_id;
    return data;
}

}  // namespace

UnsupportedCMFWError::UnsupportedCMFWError(
    const TTDevice &tt_device,
    uint64_t topology_unique_id,
    FirmwareBundleVersion found,
    FirmwareBundleVersion minimum) :
    UmdError<UnsupportedCMFWData>(
        fmt::format(
            "Firmware bundle {} on {} is older than the minimum supported {}.",
            found.str(),
            describe_device(tt_device, topology_unique_id),
            minimum.str()),
        [&] {
            UnsupportedCMFWData data;
            static_cast<TTDeviceData &>(data) = TTDeviceData(tt_device, topology_unique_id);
            data.found = found;
            data.minimum = minimum;
            return data;
        }()) {}

CMFWMismatchError::CMFWMismatchError(
    const TTDevice &tt_device,
    uint64_t topology_unique_id,
    FirmwareBundleVersion expected,
    FirmwareBundleVersion found) :
    UmdError<CMFWMismatchData>(
        fmt::format(
            "Firmware bundle {} on {} differs from {} found on the other devices.",
            found.str(),
            describe_device(tt_device, topology_unique_id),
            expected.str()),
        [&] {
            CMFWMismatchData data;
            static_cast<TTDeviceData &>(data) = TTDeviceData(tt_device, topology_unique_id);
            data.expected = expected;
            data.found = found;
            return data;
        }()) {}

EthFirmwareMismatchError::EthFirmwareMismatchError(
    const TTDevice &tt_device,
    uint64_t topology_unique_id,
    SemVer expected,
    SemVer found,
    CoreCoord core,
    NocId noc_id) :
    UmdError<EthFirmwareMismatchData>(
        fmt::format(
            "ETH firmware {} on core {} of {} differs from the expected {}.",
            found.str(),
            core.str(),
            describe_device(tt_device, topology_unique_id),
            expected.str()),
        [&] {
            auto data = make_core_data<EthFirmwareMismatchData>(tt_device, topology_unique_id, core, noc_id);
            data.expected = expected;
            data.found = found;
            return data;
        }()) {}

UnexpectedRoutingFirmwareConfigError::UnexpectedRoutingFirmwareConfigError(
    const TTDevice &tt_device, uint64_t topology_unique_id, bool expected, bool found, CoreCoord core, NocId noc_id) :
    UmdError<UnexpectedRoutingFirmwareConfigData>(
        fmt::format(
            "Routing firmware on core {} of {} is {} but was expected to be {}.",
            core.str(),
            describe_device(tt_device, topology_unique_id),
            found ? "enabled" : "disabled",
            expected ? "enabled" : "disabled"),
        [&] {
            auto data =
                make_core_data<UnexpectedRoutingFirmwareConfigData>(tt_device, topology_unique_id, core, noc_id);
            data.expected = expected;
            data.found = found;
            return data;
        }()) {}

EthFirmwareHeartbeatError::EthFirmwareHeartbeatError(
    const TTDevice &tt_device, uint64_t topology_unique_id, uint32_t heartbeat_value, CoreCoord core, NocId noc_id) :
    UmdError<EthFirmwareHeartbeatData>(
        fmt::format(
            "ETH firmware heartbeat on core {} of {} is not advancing (last value 0x{:x}).",
            core.str(),
            describe_device(tt_device, topology_unique_id),
            heartbeat_value),
        [&] {
            auto data = make_core_data<EthFirmwareHeartbeatData>(tt_device, topology_unique_id, core, noc_id);
            data.value = heartbeat_value;
            return data;
        }()) {}

}  // namespace tt::umd::error
