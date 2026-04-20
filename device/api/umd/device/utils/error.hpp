// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/error_detail.hpp"

namespace tt::umd {
class TTDevice;
}

namespace tt::umd::error {

/**
 * @brief Empty struct used when an UmdError has no associated metadata.
 *
 * This type serves as a placeholder for UmdError template instantiations
 * that do not require additional data beyond the error message.
 */
struct NoData {};

/**
 * @brief Generic runtime error with no additional metadata.
 *
 * This error type is similar to std::runtime_error, providing only an error message
 * with no additional details. It can be used as a placeholder until a more concrete
 * error type is defined for a specific situation.
 */
struct RuntimeError : public UmdError<NoData> {
    explicit RuntimeError(const std::string& message) : UmdError<NoData>(message, {}) {}
};

struct TTDeviceData {
    TTDeviceData() = default;
    TTDeviceData(TTDevice& tt_device, std::optional<uint64_t> discovery_unique_id = std::nullopt);

    IODeviceType io_device_type = IODeviceType::UNDEFINED;
    ChipId chip_id = 0;
    tt::ARCH arch = tt::ARCH::Invalid;
    std::optional<uint64_t> discovery_unique_id = std::nullopt;
};

struct DeviceCoreData : public TTDeviceData {
    xy_pair core = {0, 0};
    NocId noc_id = NocId::DEFAULT_NOC;
};

struct ArcStartupData : public DeviceCoreData {
    uint32_t scratch_status = 0;
    uint32_t postcode = 0;
    std::optional<uint32_t> message_id = std::nullopt;
};

struct ArcStartupError : UmdError<ArcStartupData> {
    ArcStartupError(
        TTDevice& tt_device,
        NocId noc_id,
        xy_pair arc_core,
        uint32_t scratch_status,
        uint32_t postcode,
        std::optional<uint32_t> message_id = std::nullopt);
    ArcStartupError(
        TTDevice& tt_device,
        NocId noc_id,
        xy_pair arc_core,
        uint32_t scratch_status,
        uint32_t postcode,
        std::chrono::milliseconds timeout,
        std::optional<uint32_t> message_id = std::nullopt);
};

struct NocHangData : TTDeviceData {
    NocId noc_id = NocId::DEFAULT_NOC;
};

struct NocHangError : UmdError<NocHangData> {
    NocHangError(TTDevice& tt_device, NocId noc_id);
};

struct PcieHangData : TTDeviceData {
    uint32_t data_read;
};

struct PcieHangError : UmdError<TTDeviceData> {
    PcieHangError(TTDevice& tt_device, uint32_t data_read);
};

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
