// SPDX-FileCopyrightText: © 2024 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace tt::umd {

/**
 * @defgroup tt_device_firmware DeviceFirmware
 * @{
 *
 * @brief Management firmware interface for a single Tenstorrent device.
 *
 * DeviceFirmware is the single point of contact between the host driver and
 * the management firmware running on the device's ARC core. It owns the full
 * firmware lifecycle: initialization handshake, command dispatch, hardware
 * training orchestration, power/clock control, and chip identity queries.
 *
 * Today these responsibilities are spread across TTDevice, ArcMessenger,
 * FirmwareInfoProvider, and architecture-specific subclasses. DeviceFirmware
 * consolidates them behind one interface so that TTDevice delegates all
 * firmware interactions here rather than owning them directly.
 *
 * ## Key Types
 *
 * | Type | Description |
 * |------|-------------|
 * | @ref DeviceCommandResult | Exit code and return values from a firmware command |
 * | @ref ChipInfo | Per-chip identity: harvesting masks, board type, board ID, ASIC location |
 * | @ref EthTrainingStatus | Ethernet link training outcome (SUCCESS / FAIL / NOT_CONNECTED) |
 * | @ref DramTrainingStatus | DRAM PHY calibration outcome (SUCCESS / FAIL / IN_PROGRESS) |
 *
 */

/**
 * @brief Abstract firmware interface for a single device.
 *
 * Concrete implementations are architecture-specific (e.g., Wormhole, Blackhole)
 * and hide the details of how the host communicates with the management core
 * (ARC mailbox, telemetry table layout, register offsets).
 */
class DeviceFirmware {
public:
    virtual ~DeviceFirmware() = default;

    /** @name Initialization */
    /** @{ */

    /**
     * @brief Performs the firmware initialization handshake.
     *
     * Verifies that the management core is accessible, waits for its boot
     * sequence to complete, and establishes the host-to-firmware communication
     * channel. Must complete before any other method on this interface is called.
     *
     * @param timeout_ms Maximum time to wait for the firmware to become ready.
     */
    virtual void init_firmware(std::chrono::milliseconds timeout_ms) = 0;

    /** @} */

    /** @name Command Dispatch */
    /** @{ */

    /**
     * @brief Sends a command to the management firmware and waits for the result.
     *
     * @param msg_code Command identifier understood by the firmware.
     * @param args Arguments for the command (device-specific limits apply).
     * @param timeout Timeout for the command to complete.
     * @return DeviceCommandResult The exit code and any return values from the firmware.
     */
    virtual DeviceCommandResult send_device_command(
        uint32_t msg_code, const std::vector<uint32_t> &args, std::chrono::milliseconds timeout) = 0;

    /** @} */

    /** @name Chip Identity */
    /** @{ */

    /**
     * @brief Queries the firmware for the chip's physical identity and configuration.
     *
     * Returns harvesting masks (which functional blocks are disabled), board
     * identity (type, ID, ASIC slot), and NOC translation state. This data is
     * typically read once during initialization and cached in the SocDescriptor.
     *
     * @return ChipInfo The chip's physical state and identity.
     */
    virtual ChipInfo get_chip_info() = 0;

    /**
     * @brief Queries whether NOC address translation is active on this chip.
     *
     * When active, the hardware applies a translation table that remaps NOC
     * coordinates to account for harvested rows or columns.
     *
     * @return true if translation is enabled.
     */
    virtual bool get_noc_translation_enabled() = 0;

    /**
     * @brief Returns the NOC coordinate of the management firmware core.
     * @return tt_xy_pair The ARC core's location on the NOC grid.
     */
    virtual tt_xy_pair get_firmware_noc_coord() const = 0;

    /** @} */

    /** @name Hardware Training */
    /** @{ */

    /**
     * @brief Waits for an Ethernet core to complete link training.
     *
     * Link training is the electrical and protocol negotiation between two
     * connected Ethernet ports. Cores that are not physically connected will
     * eventually time out or report NOT_CONNECTED.
     *
     * @param eth_core Target Ethernet core coordinates.
     * @param timeout_ms Maximum time to wait.
     * @return true if training completed successfully.
     */
    virtual bool wait_eth_core_training(tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms) = 0;

    /**
     * @brief Reads the current link training status of an Ethernet core.
     * @param eth_core Target Ethernet core coordinates.
     * @return EthTrainingStatus The current training state.
     */
    virtual EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core) = 0;

    /**
     * @brief Waits for a DRAM channel to complete PHY calibration.
     *
     * DRAM training calibrates timing and signal integrity parameters for the
     * memory interface. The firmware publishes per-channel status that this
     * method polls until success or timeout.
     *
     * @param dram_channel The DRAM channel index to wait on.
     * @param timeout_ms Maximum time to wait.
     * @return true if training completed successfully.
     */
    virtual bool wait_dram_channel_training(uint32_t dram_channel, std::chrono::milliseconds timeout_ms) = 0;

    /**
     * @brief Reads the current training status of a DRAM channel.
     * @param dram_channel The DRAM channel index to query.
     * @return DramTrainingStatus The current calibration state.
     */
    virtual DramTrainingStatus get_dram_channel_training_status(uint32_t dram_channel) = 0;

    /** @} */

    /** @name Power and Clock Control */
    /** @{ */

    /**
     * @brief Requests a hardware power domain state change.
     *
     * Claims or releases full power domains (MRISC PHY, Tensix, L2CPU).
     *
     * @param state The requested power state.
     */
    virtual void set_power_state(uint32_t state) = 0;

    /**
     * @brief Sets the device clock frequency.
     *
     * Controls the AICLK frequency the device runs at. Distinct from
     * set_power_state(), which manages hardware power domains.
     *
     * @param state The target clock state.
     */
    virtual void set_clock_state(uint32_t state) = 0;

    /** @} */
};

/** @} */  // end of tt_device_firmware group

}  // namespace tt::umd
