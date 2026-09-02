// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
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
 * Bundles the exit code and any return values from the firmware into a single
 * return type, eliminating the need for out-parameters.
 */
struct DeviceCommandResult {
    uint32_t exit_code;
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
 * @brief Abstract firmware interface for a single device.
 *
 * Firmware lifecycle, command dispatch, hardware training, power/clock control, and chip identity
 * queries.
 */
class DeviceFirmware {
public:
    virtual ~DeviceFirmware() = default;

    /**
     * @brief Performs the firmware initialization.
     * @param timeout_ms Maximum time to wait for the firmware to become ready.
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
     */
    virtual DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t> &args,
        std::chrono::milliseconds timeout,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Queries the chip's physical identity and configuration.
     * @param noc_id NOC to route through.
     * @return ChipInfo Harvesting masks, board identity, and NOC translation state.
     */
    virtual ChipInfo get_chip_info([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Queries whether NOC address translation is active on this chip.
     * @param noc_id NOC to route through.
     * @return true if translation is enabled.
     */
    virtual bool get_noc_translation_enabled([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Returns the NOC coordinate of the management firmware core.
     * @param noc_id NOC to route through.
     */
    virtual tt_xy_pair get_firmware_noc_coord([[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) const = 0;

    /**
     * @brief Waits for an Ethernet core to complete link training.
     * @param eth_core Target Ethernet core coordinates.
     * @param timeout_ms Maximum time to wait.
     * @param noc_id NOC to route through.
     * @return true if training completed successfully.
     */
    virtual bool wait_eth_core_training(
        tt_xy_pair eth_core,
        std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Reads the current link training status of an Ethernet core.
     * @param eth_core Target Ethernet core coordinates.
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
     * @return true if training completed successfully.
     */
    virtual bool wait_dram_channel_training(
        uint32_t dram_channel,
        std::chrono::milliseconds timeout_ms,
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Reads the current training status of a DRAM channel.
     * @param dram_channel The DRAM channel index to query.
     * @param noc_id NOC to route through.
     * @return DramTrainingStatus The current calibration state.
     */
    virtual DramTrainingStatus get_dram_channel_training_status(
        uint32_t dram_channel, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Requests a hardware power domain state change.
     * @param state The requested power state.
     * @param noc_id NOC to route through.
     */
    virtual void set_power_state(PowerState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Sets the device clock frequency.
     * @param state The target clock state.
     * @param noc_id NOC to route through.
     */
    virtual void set_clock_state(ClockState state, [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Retrieves the address of the runtime telemetry buffer.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Device address of the runtime telemetry buffer.
     */
    virtual std::optional<uint32_t> get_runtime_telemetry_buffer_address(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;

    /**
     * @brief Retrieves the size of the runtime telemetry buffer.
     * @param noc_id NOC to route through.
     * @return std::optional<uint32_t> Size of the runtime telemetry buffer.
     */
    virtual std::optional<uint32_t> get_runtime_telemetry_buffer_size(
        [[maybe_unused]] NocId noc_id = NocId::DEFAULT_NOC) = 0;
};

}  // namespace tt::umd
