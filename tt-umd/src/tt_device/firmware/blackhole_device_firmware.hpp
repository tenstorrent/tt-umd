// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "tt-umd/arc/blackhole_arc_message_queue.hpp"
#include "tt-umd/arc/firmware_telemetry_reader.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/tt_device/firmware/blackhole_arc_apb.hpp"
#include "tt-umd/tt_device/firmware/device_firmware.hpp"

namespace tt::umd {
class ArchitectureImplementation;
class DeviceProtocol;
class PcieInterface;

/**
 * @brief DeviceFirmware for a Blackhole reached over PCIe: the ARC firmware (CMFW) running on the ARC tile.
 *
 * Commands go through an ARC message queue and published state is read from the ARC telemetry table, both of
 * which the firmware publishes while it boots, so they are available only once init_firmware() has run. What
 * hardware registers answer (boot status, NOC translation, the reference clock, ETH training state) works from
 * construction.
 *
 * The interfaces are non-owning and must outlive this object.
 */
class BlackholeDeviceFirmware : public DeviceFirmware {
public:
    // Reads whether NOC translation is enabled, which decides where the ARC core is on NOC1.
    BlackholeDeviceFirmware(
        DeviceProtocol* device_protocol, PcieInterface* pcie_interface, ArchitectureImplementation* architecture_impl);

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t>& args,
        std::chrono::milliseconds timeout,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    // Waits up to timeout::AICLK_TIMEOUT for AICLK to leave the idle frequency (BUSY) or to reach it (IDLE); the
    // busy frequency depends on the firmware's power management, so it is not waited for exactly.
    void set_clock_state(ClockState state, NocId noc_id = NocId::DEFAULT_NOC) override;

    // Power domains of a PCIe device are claimed through the kernel driver.
    void set_power_state(PowerState state, NocId noc_id = NocId::DEFAULT_NOC) override;

    bool get_noc_translation_enabled(NocId noc_id = NocId::DEFAULT_NOC) override;

    ChipInfo get_chip_info(NocId noc_id = NocId::DEFAULT_NOC) override;

    tt_xy_pair get_firmware_noc_coord(NocId noc_id = NocId::DEFAULT_NOC) const override;

    bool wait_eth_core_training(
        tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id = NocId::DEFAULT_NOC) override;

    bool wait_dram_channel_training(
        uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    // Both throw error::UninitializedDeviceError before init_firmware(), and are std::nullopt when the firmware
    // does not publish the buffer.
    std::optional<uint64_t> get_runtime_telemetry_buffer_address(NocId noc_id = NocId::DEFAULT_NOC) override;
    std::optional<uint32_t> get_runtime_telemetry_buffer_size(NocId noc_id = NocId::DEFAULT_NOC) override;

    uint64_t get_refclk_counter(NocId noc_id = NocId::DEFAULT_NOC) override;

    // Owned here because they read state the firmware publishes; null until init_firmware() has run.
    FirmwareTelemetryReader* get_firmware_telemetry_reader() const;
    FirmwareInfoProvider* get_firmware_info_provider() const;

private:
    // Throws error::UninitializedDeviceError unless init_firmware() has run.
    void require_initialized() const;

    uint32_t read_arc_register(uint32_t arc_addr_offset, NocId noc_id);

    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    BlackholeArcApb arc_apb_;
    bool noc_translation_enabled_ = false;

    std::unique_ptr<BlackholeArcMessageQueue> message_queue_;
    std::unique_ptr<FirmwareTelemetryReader> telemetry_reader_;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider_;
};

}  // namespace tt::umd
