// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {
class TTDevice;
}

namespace tt::umd::error {

struct TTDeviceData {
    TTDeviceData() = default;
    TTDeviceData(const TTDevice& tt_device, std::optional<uint64_t> discovery_unique_id = std::nullopt);

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
    std::optional<uint32_t> smc_init_status = std::nullopt;
};

struct ArcStartupError : UmdError<ArcStartupData> {
    ArcStartupError(
        const TTDevice& tt_device,
        NocId noc_id,
        xy_pair arc_core,
        uint32_t scratch_status,
        uint32_t postcode,
        std::optional<uint32_t> message_id = std::nullopt,
        std::optional<uint32_t> smc_init_status = std::nullopt);
    ArcStartupError(
        const TTDevice& tt_device,
        NocId noc_id,
        xy_pair arc_core,
        uint32_t scratch_status,
        uint32_t postcode,
        std::chrono::milliseconds timeout,
        std::optional<uint32_t> message_id = std::nullopt,
        std::optional<uint32_t> smc_init_status = std::nullopt);
};

struct NocHangData : TTDeviceData {
    NocId noc_id = NocId::DEFAULT_NOC;
};

struct NocHangError : UmdError<NocHangData> {
    NocHangError(const TTDevice& tt_device, NocId noc_id);
};

struct PcieHangData : TTDeviceData {
    uint32_t data_read;
};

struct PcieHangError : UmdError<PcieHangData> {
    PcieHangError(const TTDevice& tt_device, uint32_t data_read);
};

struct UninitializedDeviceError : UmdError<TTDeviceData> {
    UninitializedDeviceError(const TTDevice& tt_device);
};

}  // namespace tt::umd::error
