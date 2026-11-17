// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device_error.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd::error {

namespace {

std::string describe_device(const TTDeviceData &device) {
    return fmt::format(
        "{} device {} ({})", DeviceTypeToString.at(device.io_device_type), device.chip_id, arch_to_str(device.arch));
}

std::string describe_firmware_status(
    uint32_t scratch_status,
    uint32_t postcode,
    std::optional<uint32_t> message_id,
    std::optional<uint32_t> smc_init_status) {
    std::string status = fmt::format("status 0x{:x}, postcode 0x{:x}", scratch_status, postcode);
    if (message_id.has_value()) {
        status += fmt::format(", message 0x{:x}", message_id.value());
    }
    if (smc_init_status.has_value()) {
        status += fmt::format(", SMC init status 0x{:x}", smc_init_status.value());
    }
    return status;
}

FirmwareStartupData make_firmware_startup_data(
    IODeviceType io_device_type,
    ChipId chip_id,
    tt::ARCH arch,
    NocId noc_id,
    xy_pair fw_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::optional<uint32_t> message_id,
    std::optional<uint32_t> smc_init_status) {
    FirmwareStartupData data;
    static_cast<TTDeviceData &>(data) = TTDeviceData(io_device_type, chip_id, arch);
    data.core = CoreCoord(fw_core);
    data.noc_id = noc_id;
    data.scratch_status = scratch_status;
    data.postcode = postcode;
    data.message_id = message_id;
    data.smc_init_status = smc_init_status;
    return data;
}

template <typename DATA_T>
DATA_T make_device_data(const TTDeviceData &device) {
    DATA_T data;
    static_cast<TTDeviceData &>(data) = device;
    return data;
}

}  // namespace

TTDeviceData::TTDeviceData(const TTDevice &tt_device, std::optional<uint64_t> discovery_unique_id) :
    io_device_type(tt_device.get_communication_device_type()),
    chip_id(tt_device.get_communication_device_id()),
    arch(tt_device.get_arch()),
    discovery_unique_id(discovery_unique_id) {}

TTDeviceData::TTDeviceData(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch) :
    io_device_type(io_device_type), chip_id(chip_id), arch(arch) {}

FirmwareStartupError::FirmwareStartupError(
    IODeviceType io_device_type,
    ChipId chip_id,
    tt::ARCH arch,
    NocId noc_id,
    xy_pair fw_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::optional<uint32_t> message_id,
    std::optional<uint32_t> smc_init_status) :
    UmdError<FirmwareStartupData>(
        fmt::format(
            "Firmware on {} at core {} over {} did not start: {}.",
            describe_device(TTDeviceData(io_device_type, chip_id, arch)),
            fw_core.str(),
            noc_to_str(noc_id),
            describe_firmware_status(scratch_status, postcode, message_id, smc_init_status)),
        make_firmware_startup_data(
            io_device_type, chip_id, arch, noc_id, fw_core, scratch_status, postcode, message_id, smc_init_status)) {}

FirmwareStartupError::FirmwareStartupError(
    IODeviceType io_device_type,
    ChipId chip_id,
    tt::ARCH arch,
    NocId noc_id,
    xy_pair fw_core,
    uint32_t scratch_status,
    uint32_t postcode,
    std::chrono::milliseconds timeout,
    std::optional<uint32_t> message_id,
    std::optional<uint32_t> smc_init_status) :
    UmdError<FirmwareStartupData>(
        fmt::format(
            "Firmware on {} at core {} over {} did not start within {} ms: {}.",
            describe_device(TTDeviceData(io_device_type, chip_id, arch)),
            fw_core.str(),
            noc_to_str(noc_id),
            timeout.count(),
            describe_firmware_status(scratch_status, postcode, message_id, smc_init_status)),
        make_firmware_startup_data(
            io_device_type, chip_id, arch, noc_id, fw_core, scratch_status, postcode, message_id, smc_init_status)) {}

NocHangError::NocHangError(const TTDevice &tt_device, NocId noc_id) :
    UmdError<NocHangData>(fmt::format("{} is hung: reads over it return the hang signature.", noc_to_str(noc_id)), [&] {
        NocHangData data = make_device_data<NocHangData>(TTDeviceData(tt_device));
        data.noc_id = noc_id;
        return data;
    }()) {}

PcieHangError::PcieHangError(const TTDevice &tt_device, uint32_t data_read) :
    PcieHangError(
        tt_device.get_communication_device_type(),
        tt_device.get_communication_device_id(),
        tt_device.get_arch(),
        data_read) {}

PcieHangError::PcieHangError(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch, uint32_t data_read) :
    UmdError<PcieHangData>(
        fmt::format(
            "PCIe link to {} is hung: read 0x{:x} from a register that never holds it.",
            describe_device(TTDeviceData(io_device_type, chip_id, arch)),
            data_read),
        [&] {
            PcieHangData data = make_device_data<PcieHangData>(TTDeviceData(io_device_type, chip_id, arch));
            data.data_read = data_read;
            return data;
        }()) {}

UninitializedDeviceError::UninitializedDeviceError(const TTDevice &tt_device) :
    UninitializedDeviceError(
        tt_device.get_communication_device_type(), tt_device.get_communication_device_id(), tt_device.get_arch()) {}

UninitializedDeviceError::UninitializedDeviceError(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch) :
    UmdError<TTDeviceData>(
        fmt::format(
            "{} is not initialized; call init_tt_device() first.",
            describe_device(TTDeviceData(io_device_type, chip_id, arch))),
        TTDeviceData(io_device_type, chip_id, arch)) {}

UnresolvableCoordinateError::UnresolvableCoordinateError(const TTDevice &tt_device, CoreCoord core, NocId noc) :
    UmdError<DeviceCoreData>(
        fmt::format(
            "Core {} on {} cannot be resolved to a NOC coordinate on {}.",
            core.str(),
            describe_device(TTDeviceData(tt_device)),
            noc_to_str(noc)),
        [&] {
            DeviceCoreData data = make_device_data<DeviceCoreData>(TTDeviceData(tt_device));
            data.core = core;
            data.noc_id = noc;
            return data;
        }()) {}

}  // namespace tt::umd::error
