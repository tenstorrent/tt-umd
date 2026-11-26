// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include "tt-umd/tt_device/firmware/device_firmware.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"

namespace tt::umd {

/**
 * @brief Simulation implementation of DeviceFirmware.
 *
 * Simulators run no management firmware, so as the interface grows this implements each operation
 * as the appropriate no-op. Backends that need to differ get their own subclass when that need
 * arrives, not before.
 */
class SimulationDeviceFirmware : public DeviceFirmware {
public:
    /**
     * @brief Builds firmware for a simulated device.
     * @param chip_info The chip the device was built as. A simulator publishes no chip info, so
     * get_chip_info() and get_noc_translation_enabled() report this one, and initializing the device
     * keeps the SoC descriptor it was built from.
     */
    explicit SimulationDeviceFirmware(const ChipInfo& chip_info) : chip_info_(chip_info) {}

    // A simulated device's firmware is ready by construction, so there is nothing to wait for.
    void init_firmware(
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    // There is no management firmware to command; report success with no return values so shared
    // flows that issue commands unconditionally keep working against a simulated device.
    DeviceCommandResult send_device_command(
        [[maybe_unused]] uint32_t msg_code,
        [[maybe_unused]] const std::vector<uint32_t>& args,
        [[maybe_unused]] std::chrono::milliseconds timeout,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return DeviceCommandResult{};
    }

    // Simulated devices have no controllable clock.
    void set_clock_state(
        [[maybe_unused]] ClockState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    // Simulated devices have no power domains to manage.
    void set_power_state(
        [[maybe_unused]] PowerState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    bool get_noc_translation_enabled([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return chip_info_.noc_translation_enabled;
    }

    ChipInfo get_chip_info([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return chip_info_; }

    // A simulated device has no management firmware core; callers that need a coordinate get the
    // origin, matching what TTDevice::get_arc_core() returned for simulation before this existed.
    tt_xy_pair get_firmware_noc_coord([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const override {
        return tt_xy_pair{};
    }

    // Simulation runs no link training. The RTL backend's deleted TTDevice overrides reported
    // exactly this (no wait, SUCCESS); TTSim's threw "not supported", which nothing reached -- the
    // simulation chip flow never waits on training.
    bool wait_eth_core_training(
        [[maybe_unused]] tt_xy_pair eth_core,
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return true;
    }

    EthTrainingStatus get_eth_core_training_status(
        [[maybe_unused]] tt_xy_pair eth_core, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return EthTrainingStatus::SUCCESS;
    }

    // No DRAM training in simulation either; report it as never completing rather than lying that
    // it succeeded, matching the "status not available" early-out of the silicon path.
    bool wait_dram_channel_training(
        [[maybe_unused]] uint32_t dram_channel,
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return false;
    }

    std::optional<uint64_t> get_runtime_telemetry_buffer_address(NocId noc_id = NocId::DEFAULT_NOC) override {
        return std::nullopt;
    }

    std::optional<uint32_t> get_runtime_telemetry_buffer_size(NocId noc_id = NocId::DEFAULT_NOC) override {
        return std::nullopt;
    }

    // A simulated device has no ARC reset unit to read the counter from. Throwing matches the "ARC
    // APB access is not supported" failure the read produced before it moved here.
    uint64_t get_refclk_counter([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        UMD_THROW(error::RuntimeError, "The refclk counter is not available for a simulated device.");
    }

private:
    ChipInfo chip_info_;
};

}  // namespace tt::umd
