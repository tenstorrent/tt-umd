// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device/tt_device_error.hpp"

#include <fmt/format.h>

#include <string>
#include <unordered_map>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/blackhole_arc.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt::umd;

namespace tt::umd::error {

TTDeviceData::TTDeviceData(const TTDevice& tt_device, std::optional<uint64_t> discovery_unique_id) :
    io_device_type(tt_device.get_communication_device_type()),
    chip_id(tt_device.get_communication_device_id()),
    arch(tt_device.get_arch()),
    discovery_unique_id(discovery_unique_id) {}

NocHangError::NocHangError(const TTDevice& tt_device, NocId noc_id) :
    UmdError<NocHangData>(fmt::format("{} is hung", noc_to_str(noc_id)), {{tt_device}, noc_id}) {
    message().append(fmt::format(" on {} device ID {}.", DeviceTypeToString.at(data().io_device_type), data().chip_id));
}

PcieHangError::PcieHangError(const TTDevice& tt_device, uint32_t data_read) :
    UmdError<PcieHangData>(
        fmt::format(
            "Read {:#x} over PCIe ID {}: the board should be reset.",
            data_read,
            tt_device.get_communication_device_id()),
        {{tt_device}, data_read}) {}

ArcStartupError::ArcStartupError(
    const TTDevice& tt_device,
    NocId noc_id,
    xy_pair arc_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::optional<uint32_t> message_id,
    std::optional<uint32_t> smc_init_status) :
    UmdError<ArcStartupData>(
        fmt::format(
            "ARC startup error at core {} over {}: scratch_status={:#x}, postcode={:#x}{}{}",
            arc_core.str(),
            noc_to_str(noc_id),
            scratch_status,
            postcode,
            message_id.has_value() ? fmt::format(", message_id={:#x}", message_id.value()) : "",
            smc_init_status.has_value() ? fmt::format(
                                              ", smc_init_status={:#x} ({})",
                                              smc_init_status.value(),
                                              blackhole::interpret_smc_init_status(smc_init_status.value()))
                                        : ""),
        {{{tt_device}, arc_core, noc_id}, scratch_status, postcode, message_id, smc_init_status}) {}

ArcStartupError::ArcStartupError(
    const TTDevice& tt_device,
    NocId noc_id,
    xy_pair arc_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::chrono::milliseconds timeout,
    std::optional<uint32_t> message_id,
    std::optional<uint32_t> smc_init_status) :
    ArcStartupError(tt_device, noc_id, arc_core, scratch_status, postcode, message_id, smc_init_status) {
    message().append(fmt::format(" (Timed out after {} ms)", timeout.count()));
}

UninitializedDeviceError::UninitializedDeviceError(const TTDevice& tt_device) :
    UmdError<TTDeviceData>(
        fmt::format(
            "This method cannot be called before initializing TTDevice. Device ID: {}",
            tt_device.get_communication_device_id()),
        tt_device) {}

UnresolvableCoordinateError::UnresolvableCoordinateError(const TTDevice& tt_device, CoreCoord core, NocId noc) :
    UmdError<DeviceCoreData>(
        fmt::format(
            "Cannot resolve non-trivial coordinate system {} before initializing device. Device ID: {}",
            to_str(core.coord_system),
            tt_device.get_communication_device_id()),
        {{tt_device}, core, noc}) {}

}  // namespace tt::umd::error
