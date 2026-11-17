// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "tt_device/firmware/blackhole_device_firmware.hpp"

#include "tt-umd/utils/error.hpp"

namespace tt::umd {

BlackholeDeviceFirmware::BlackholeDeviceFirmware(
    DeviceProtocol* device_protocol,
    PcieInterface* pcie_interface,
    JtagInterface* jtag_interface,
    ArchitectureImplementation* architecture_impl) :
    device_protocol_(device_protocol),
    architecture_impl_(architecture_impl),
    arc_apb_(device_protocol, pcie_interface, jtag_interface) {}

BlackholeDeviceFirmware::~BlackholeDeviceFirmware() = default;

void BlackholeDeviceFirmware::init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::init_firmware is not implemented yet");
}

DeviceCommandResult BlackholeDeviceFirmware::send_device_command(
    uint32_t msg_code, const std::vector<uint32_t>& args, std::chrono::milliseconds timeout, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::send_device_command is not implemented yet");
}

void BlackholeDeviceFirmware::set_clock_state(ClockState state, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::set_clock_state is not implemented yet");
}

void BlackholeDeviceFirmware::set_power_state(PowerState state, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::set_power_state is not implemented yet");
}

bool BlackholeDeviceFirmware::get_noc_translation_enabled(NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::get_noc_translation_enabled is not implemented yet");
}

ChipInfo BlackholeDeviceFirmware::get_chip_info(NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::get_chip_info is not implemented yet");
}

tt_xy_pair BlackholeDeviceFirmware::get_firmware_noc_coord(NocId noc_id) const {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::get_firmware_noc_coord is not implemented yet");
}

bool BlackholeDeviceFirmware::wait_eth_core_training(
    tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::wait_eth_core_training is not implemented yet");
}

EthTrainingStatus BlackholeDeviceFirmware::get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::get_eth_core_training_status is not implemented yet");
}

bool BlackholeDeviceFirmware::wait_dram_channel_training(
    uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::wait_dram_channel_training is not implemented yet");
}

std::optional<uint64_t> BlackholeDeviceFirmware::get_runtime_telemetry_buffer_address(NocId noc_id) {
    UMD_THROW(
        error::RuntimeError, "BlackholeDeviceFirmware::get_runtime_telemetry_buffer_address is not implemented yet");
}

std::optional<uint32_t> BlackholeDeviceFirmware::get_runtime_telemetry_buffer_size(NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::get_runtime_telemetry_buffer_size is not implemented yet");
}

uint64_t BlackholeDeviceFirmware::get_refclk_counter(NocId noc_id) {
    UMD_THROW(error::RuntimeError, "BlackholeDeviceFirmware::get_refclk_counter is not implemented yet");
}

FirmwareTelemetryReader* BlackholeDeviceFirmware::get_firmware_telemetry_reader() const {
    return telemetry_reader_.get();
}

FirmwareInfoProvider* BlackholeDeviceFirmware::get_firmware_info_provider() const {
    return firmware_info_provider_.get();
}

}  // namespace tt::umd
