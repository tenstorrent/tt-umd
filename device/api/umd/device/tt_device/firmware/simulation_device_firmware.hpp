// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/types/arch.hpp"

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
     * @brief Builds firmware for a simulated device of the given architecture.
     * @param arch Architecture being simulated; get_chip_info() reports harvesting per architecture.
     */
    explicit SimulationDeviceFirmware(tt::ARCH arch) : arch_(arch) {}

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

    // Simulation backends operate on logical/virtual coordinates end-to-end; NOC translation is
    // never applied.
    bool get_noc_translation_enabled([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return false; }

    // There is no FirmwareInfoProvider on a simulator, so the defaults mirror the ones
    // TTSimTTDevice::create() uses. Blackhole SocDescriptor construction rejects an empty
    // eth_harvesting_mask ("Exactly 2 or 14 ETH cores should be harvested on full Blackhole"), so
    // the same 0x120 default is applied here. Keep in sync with create().
    ChipInfo get_chip_info([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        ChipInfo chip_info{};
        if (arch_ == tt::ARCH::BLACKHOLE) {
            chip_info.harvesting_masks.eth_harvesting_mask = 0x120;
        }
        return chip_info;
    }

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

    // A simulated device has no ARC reset unit to read the counter from. Throwing matches the "ARC
    // APB access is not supported" failure the read produced before it moved here.
    uint64_t get_refclk_counter([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        UMD_THROW(error::RuntimeError, "The refclk counter is not available for a simulated device.");
    }

private:
    tt::ARCH arch_ = tt::ARCH::Invalid;
};

}  // namespace tt::umd
