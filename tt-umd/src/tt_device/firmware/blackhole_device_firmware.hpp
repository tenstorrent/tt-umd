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
class JtagInterface;
class PcieInterface;

/**
 * @brief DeviceFirmware for Blackhole's ARC firmware.
 *
 * Commands go through the ARC message queue, and published state is read through the telemetry
 * table. Both are located through registers the firmware fills in while booting, so they exist only
 * after init_firmware(); until then the telemetry reader and info provider are null.
 *
 * The interfaces are non-owning and must outlive this object.
 */
class BlackholeDeviceFirmware : public DeviceFirmware {
public:
    /**
     * @param device_protocol Protocol for NOC accesses to the ARC tile. Required.
     * @param pcie_interface BAR access, when the device is reached over PCIe.
     * @param jtag_interface JTAG access, when the device is reached over JTAG.
     * @param architecture_impl Blackhole architecture constants. Required.
     */
    BlackholeDeviceFirmware(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        ArchitectureImplementation* architecture_impl);

    ~BlackholeDeviceFirmware() override;

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;
    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t>& args,
        std::chrono::milliseconds timeout,
        NocId noc_id = NocId::DEFAULT_NOC) override;
    void set_clock_state(ClockState state, NocId noc_id = NocId::DEFAULT_NOC) override;
    void set_power_state(PowerState state, NocId noc_id = NocId::DEFAULT_NOC) override;
    bool get_noc_translation_enabled(NocId noc_id = NocId::DEFAULT_NOC) override;
    ChipInfo get_chip_info(NocId noc_id = NocId::DEFAULT_NOC) override;
    tt_xy_pair get_firmware_noc_coord(NocId noc_id = NocId::DEFAULT_NOC) const override;
    bool wait_eth_core_training(
        tt_xy_pair eth_core, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;
    EthTrainingStatus get_eth_core_training_status(tt_xy_pair eth_core, NocId noc_id = NocId::DEFAULT_NOC) override;
    bool wait_dram_channel_training(
        uint32_t dram_channel, std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;
    std::optional<uint64_t> get_runtime_telemetry_buffer_address(NocId noc_id = NocId::DEFAULT_NOC) override;
    std::optional<uint32_t> get_runtime_telemetry_buffer_size(NocId noc_id = NocId::DEFAULT_NOC) override;
    uint64_t get_refclk_counter(NocId noc_id = NocId::DEFAULT_NOC) override;

    // Lent to the model, which hands them to TTDevice. Null until init_firmware() has run.
    FirmwareTelemetryReader* get_firmware_telemetry_reader() const;
    FirmwareInfoProvider* get_firmware_info_provider() const;

private:
    DeviceProtocol* device_protocol_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    BlackholeArcApb arc_apb_;

    std::unique_ptr<BlackholeArcMessageQueue> arc_message_queue_;
    std::unique_ptr<FirmwareTelemetryReader> telemetry_reader_;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider_;
};

}  // namespace tt::umd
