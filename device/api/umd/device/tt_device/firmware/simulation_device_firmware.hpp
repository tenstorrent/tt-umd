// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace tt::umd {

/**
 * @brief Firmware behavior common to all simulation backends.
 *
 * Mirrors SimulationTTDevice: the parts that are identical for every simulator live here,
 * the backend specific ones are overridden in RtlSimulationDeviceFirmware and TTSimDeviceFirmware.
 */
class SimulationDeviceFirmware : public DeviceFirmware {
public:
    /** Simulation backends publish no firmware telemetry. */
    FirmwareTelemetryReader *get_firmware_telemetry_reader() const override { return nullptr; }

    /** Simulation backends publish no firmware information. */
    FirmwareInfoProvider *get_firmware_info_provider() const override { return nullptr; }

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t> &args,
        std::chrono::milliseconds timeout,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    ChipInfo get_chip_info(NocId noc_id = NocId::DEFAULT_NOC) override;

    bool get_noc_translation_enabled(NocId noc_id = NocId::DEFAULT_NOC) override;

    tt_xy_pair get_firmware_noc_coord(NocId noc_id = NocId::DEFAULT_NOC) const override;

    bool wait_eth_core_training(
        tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id = NocId::DEFAULT_NOC) override;

    bool wait_dram_channel_training(
        uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DramTrainingStatus get_dram_channel_training_status(
        uint32_t dram_channel, NocId noc_id = NocId::DEFAULT_NOC) override;

    void set_power_state(PowerState state, NocId noc_id = NocId::DEFAULT_NOC) override;

    void set_clock_state(ClockState state, NocId noc_id = NocId::DEFAULT_NOC) override;

protected:
    /**
     * @brief Waits for the management firmware; a no-op on every simulation backend.
     *
     * Present so the seam is in the same place as on silicon: waiting for the firmware and building
     * what depends on it are two jobs, and this is the half that becomes its own API. Simulators
     * have no management firmware to wait for, so there is nothing to do here.
     */
    void wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id);
};

}  // namespace tt::umd
