// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/eth_training_status.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/power_state.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Result of a firmware command execution.
 *
 * Bundles the exit code and any return values from the firmware into a single return type,
 * eliminating the need for out-parameters.
 */
struct DeviceCommandResult {
    uint32_t exit_code = 0;
    std::vector<uint32_t> return_values;
};

/**
 * @brief Requested clock state for a device.
 */
enum class ClockState {
    BUSY,  ///< Maximum AICLK frequency.
    IDLE,  ///< Minimum AICLK frequency.
};

/**
 * @brief Interface to the device's management firmware.
 *
 * Owns the interaction with the firmware running on the device's management processor: waiting for
 * it to boot, issuing commands, and reading the state it publishes. TTDevice forwards the
 * firmware-backed parts of its API here; the implementations are built from protocol interfaces
 * rather than a TTDevice, so they carry no dependency on the facade.
 *
 * The interface grows one capability at a time, each added together with the TTDevice code it
 * replaces, so every addition is reviewable as a move.
 */
class DeviceFirmware {
public:
    virtual ~DeviceFirmware() = default;

    /**
     * @brief Performs the firmware initialization.
     *
     * Blocks until the management firmware reports it has booted.
     *
     * @param timeout_ms Maximum time to wait for the firmware to become ready.
     * @param noc_id NOC to route the status reads over.
     * @throws error::FirmwareStartupError if the firmware does not come up within the timeout.
     */
    virtual void init_firmware(
        std::chrono::milliseconds timeout_ms, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Sends a command to the management firmware and waits for the result.
     * @param msg_code Command identifier understood by the firmware.
     * @param args Arguments for the command.
     * @param timeout Timeout for the command to complete.
     * @param noc_id NOC to route through.
     * @return DeviceCommandResult The exit code and any return values.
     * @throws error::UninitializedDeviceError if init_firmware() has not run: commands must not be
     * sent to firmware that has not reported ready.
     */
    virtual DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t> &args,
        std::chrono::milliseconds timeout,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Sets the device clock frequency.
     *
     * Distinct from a power-state change: this drives AICLK through the firmware, then waits for
     * the clock to settle near the target before returning.
     *
     * @param state The target clock state.
     * @param noc_id NOC to route through.
     */
    virtual void set_clock_state(ClockState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Requests a hardware power domain state change.
     *
     * Distinct from set_clock_state(): this manages power domains rather than the clock frequency.
     *
     * @param state The requested power state.
     * @param noc_id NOC to route through.
     */
    virtual void set_power_state(PowerState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Queries whether NOC address translation is active on this chip.
     * @param noc_id NOC to route through.
     * @return true if translation is enabled.
     */
    virtual bool get_noc_translation_enabled([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Queries the chip's physical identity and configuration.
     * @param noc_id NOC to route through.
     * @return ChipInfo Harvesting masks, board identity, and NOC translation state.
     * @throws error::UninitializedDeviceError if init_firmware() has not run: the answers come from
     * state the firmware publishes.
     */
    virtual ChipInfo get_chip_info([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Returns the NOC coordinate of the management firmware core.
     * @param noc_id NOC to resolve the coordinate for.
     */
    virtual tt_xy_pair get_firmware_noc_coord([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Waits for an Ethernet core to complete link training.
     * @param eth_core Target Ethernet core coordinate, resolved for noc_id.
     * @param timeout_ms Maximum time to wait.
     * @param noc_id NOC to route through.
     * @return true if training completed within the timeout.
     */
    virtual bool wait_eth_core_training(
        tt_xy_pair eth_core,
        std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Reads the current link training status of an Ethernet core.
     * @param eth_core Target Ethernet core coordinate, resolved for noc_id.
     * @param noc_id NOC to route through.
     * @return EthTrainingStatus The current training state.
     */
    virtual EthTrainingStatus get_eth_core_training_status(
        tt_xy_pair eth_core, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Waits for a DRAM channel to complete training.
     * @param dram_channel The DRAM channel index to wait on.
     * @param timeout_ms Maximum time to wait.
     * @param noc_id NOC to route through.
     * @return true if training completed within the timeout.
     */
    virtual bool wait_dram_channel_training(
        uint32_t dram_channel,
        std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;
};

}  // namespace tt::umd
