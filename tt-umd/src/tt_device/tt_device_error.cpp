// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt-umd/tt_device/tt_device_error.hpp"

#include <fmt/format.h>

#include <chrono>
#include <optional>
#include <string>

#include "tt-umd/tt_device/tt_device.hpp"

namespace tt::umd::error {

namespace {

std::string describe_device(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch) {
    return fmt::format("{} device {} ({})", DeviceTypeToString.at(io_device_type), chip_id, arch_to_str(arch));
}

std::string describe_device(const TTDeviceData& data) {
    return describe_device(data.io_device_type, data.chip_id, data.arch);
}

std::string describe_optional(std::optional<uint32_t> value) {
    return value.has_value() ? fmt::format("{:#x}", value.value()) : "n/a";
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
    data.io_device_type = io_device_type;
    data.chip_id = chip_id;
    data.arch = arch;
    data.core = CoreCoord(fw_core);
    data.noc_id = noc_id;
    data.scratch_status = scratch_status;
    data.postcode = postcode;
    data.message_id = message_id;
    data.smc_init_status = smc_init_status;
    return data;
}

std::string describe_firmware_startup(const FirmwareStartupData& data) {
    return fmt::format(
        "Firmware on {} (core {}, {}) did not start: scratch status {:#x}, postcode {:#x}, message id {}, SMC init "
        "status {}.",
        describe_device(data),
        data.core.str(),
        noc_to_str(data.noc_id),
        data.scratch_status,
        data.postcode,
        describe_optional(data.message_id),
        describe_optional(data.smc_init_status));
}

TTDeviceData make_device_data(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch) {
    return TTDeviceData(io_device_type, chip_id, arch);
}

}  // namespace

TTDeviceData::TTDeviceData(const TTDevice& tt_device, std::optional<uint64_t> discovery_unique_id) :
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
        describe_firmware_startup(make_firmware_startup_data(
            io_device_type, chip_id, arch, noc_id, fw_core, scratch_status, postcode, message_id, smc_init_status)),
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
    FirmwareStartupError(
        io_device_type, chip_id, arch, noc_id, fw_core, scratch_status, postcode, message_id, smc_init_status) {
    message() = fmt::format("{} Timed out after {} ms.", message(), timeout.count());
}

NocHangError::NocHangError(const TTDevice& tt_device, NocId noc_id) :
    UmdError<NocHangData>("", NocHangData{TTDeviceData(tt_device), noc_id}) {
    message() = fmt::format("{} is hung on {}.", describe_device(data()), noc_to_str(noc_id));
}

PcieHangError::PcieHangError(const TTDevice& tt_device, uint32_t data_read) :
    UmdError<PcieHangData>("", PcieHangData{TTDeviceData(tt_device), data_read}) {
    message() = fmt::format("PCIe link of {} is hung: read {:#x}.", describe_device(data()), data_read);
}

PcieHangError::PcieHangError(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch, uint32_t data_read) :
    UmdError<PcieHangData>("", PcieHangData{make_device_data(io_device_type, chip_id, arch), data_read}) {
    message() = fmt::format("PCIe link of {} is hung: read {:#x}.", describe_device(data()), data_read);
}

UninitializedDeviceError::UninitializedDeviceError(const TTDevice& tt_device) :
    UmdError<TTDeviceData>("", TTDeviceData(tt_device)) {
    message() = fmt::format("{} is not initialized; call init_tt_device() first.", describe_device(data()));
}

UninitializedDeviceError::UninitializedDeviceError(IODeviceType io_device_type, ChipId chip_id, tt::ARCH arch) :
    UmdError<TTDeviceData>("", make_device_data(io_device_type, chip_id, arch)) {
    message() = fmt::format("{} is not initialized; call init_tt_device() first.", describe_device(data()));
}

UnresolvableCoordinateError::UnresolvableCoordinateError(const TTDevice& tt_device, CoreCoord core, NocId noc) :
    UmdError<DeviceCoreData>("", DeviceCoreData{TTDeviceData(tt_device), core, noc}) {
    message() = fmt::format(
        "Coordinate {} on {} cannot be resolved for {}: translating it needs the SoC descriptor, which exists only "
        "after init_tt_device().",
        core.str(),
        describe_device(data()),
        noc_to_str(noc));
}

}  // namespace tt::umd::error
