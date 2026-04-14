// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/utils/error.hpp"

#include <fmt/format.h>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/noc_id.hpp"

using namespace tt::umd;

namespace tt::umd::error {

TTDeviceData::TTDeviceData(TTDevice& tt_device, std::optional<uint64_t> discovery_unique_id) :
    io_device_type(tt_device.get_communication_device_type()),
    chip_id(tt_device.get_communication_device_id()),
    arch(tt_device.get_arch()),
    discovery_unique_id(discovery_unique_id) {}

NocHangError::NocHangError(TTDevice& tt_device, NocId noc_id) :
    UmdError<NocHangData>(fmt::format("{} is hung.", noc_to_str(noc_id)), {{tt_device}, noc_id}) {}

ArcStartupError::ArcStartupError(
    TTDevice& tt_device,
    NocId noc_id,
    xy_pair arc_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::optional<uint32_t> message_id) :
    UmdError<ArcStartupData>(
        fmt::format(
            "ARC startup error at core {} over {}: scratch_status={:#x}, postcode={:#x}{}",
            arc_core.str(),
            noc_to_str(noc_id),
            scratch_status,
            postcode,
            message_id.has_value() ? fmt::format(", message_id={:#x}", message_id.value()) : ""),
        {{{tt_device}, arc_core, noc_id}, scratch_status, postcode, message_id}) {}

ArcStartupError::ArcStartupError(
    std::chrono::milliseconds timeout,
    TTDevice& tt_device,
    NocId noc_id,
    xy_pair arc_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::optional<uint32_t> message_id) :
    UmdError<ArcStartupData>(
        fmt::format(
            "ARC startup timed out after {} ms on core {} over {}: scratch_status={:#x}, postcode={:#x}{}",
            timeout.count(),
            arc_core.str(),
            noc_to_str(noc_id),
            scratch_status,
            postcode,
            message_id.has_value() ? fmt::format(", message_id={:#x}", message_id.value()) : ""),
        {{{tt_device}, arc_core, noc_id}, scratch_status, postcode, message_id}) {}

}  // namespace tt::umd::error
