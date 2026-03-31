// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/utils/error_detail.hpp"
#include "umd/device/utils/semver.hpp"

namespace tt::umd::error {

struct TTDeviceData {
    TTDeviceData(TTDevice& tt_device, std::optional<uint64_t> discovery_unique_id = std::nullopt) :
        io_device_type(tt_device.get_communication_device_type()),
        chip_id(tt_device.get_communication_device_id()),
        arch(tt_device.get_arch()),
        discovery_unique_id(discovery_unique_id) {}

    IODeviceType io_device_type;
    ChipId chip_id;
    tt::ARCH arch;
    std::optional<uint64_t> discovery_unique_id;
};

struct CMFWMismatchData : public TTDeviceData {
    SemVer expected;
    SemVer got;
};

struct CMFWMismatchError : UmdError<CMFWMismatchData> {
    explicit CMFWMismatchError(
        TTDevice& tt_device, uint64_t discovery_unique_id, const SemVer& expected, const SemVer& got);
};

struct CMFWUnsupportedData : public TTDeviceData {
    SemVer got;
    SemVer minimum;
};

struct CMFWUnsupportedError : UmdError<CMFWUnsupportedData> {
    explicit CMFWUnsupportedError(
        TTDevice& tt_device, uint64_t discovery_unique_id, const SemVer& got, const SemVer& minimum);
};

using TopologyErrors = std::variant<CMFWUnsupportedError, CMFWMismatchError>;

/**
 * @brief Exception thrown when a SIGBUS signal is intercepted.
 * This indicates a hardware access error, likely due to a reset or
 * hanging device while accessing mapped memory.
 */
class SigbusError : public std::runtime_error {
public:
    explicit SigbusError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace tt::umd::error
