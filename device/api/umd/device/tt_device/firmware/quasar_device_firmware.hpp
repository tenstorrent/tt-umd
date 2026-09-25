// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * Firmware lifecycle for Quasar, which has none yet.
 *
 * The kernel driver's Quasar hooks are stubs -- hardware init, telemetry and reset state all log
 * and return -- so there is no firmware for this side to talk to. Every call here is the
 * corresponding no-op rather than a throw: the device is reachable and its memory can be read and
 * written, and refusing to construct over a missing mailbox would make that unreachable too.
 *
 * As firmware appears on the device, calls move out of here one at a time.
 */
class QuasarDeviceFirmware : public DeviceFirmware {
public:
    void init_firmware(
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    DeviceCommandResult send_device_command(
        [[maybe_unused]] uint32_t msg_code,
        [[maybe_unused]] const std::vector<uint32_t>& args,
        [[maybe_unused]] std::chrono::milliseconds timeout,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return DeviceCommandResult{};
    }

    void set_clock_state(
        [[maybe_unused]] ClockState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    void set_power_state(
        [[maybe_unused]] PowerState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {}

    /** No firmware programs a translation table, so coordinates arrive the way the caller wrote them. */
    bool get_noc_translation_enabled([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return false; }

    ChipInfo get_chip_info([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return ChipInfo{}; }

    tt_xy_pair get_firmware_noc_coord([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const override {
        return tt_xy_pair{};
    }

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

    bool wait_dram_channel_training(
        [[maybe_unused]] uint32_t dram_channel,
        [[maybe_unused]] std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return true;
    }

    std::optional<uint32_t> get_runtime_telemetry_buffer_address(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return std::nullopt;
    }

    std::optional<uint32_t> get_runtime_telemetry_buffer_size(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override {
        return std::nullopt;
    }

    uint64_t get_refclk_counter([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) override { return 0; }
};

}  // namespace tt::umd
