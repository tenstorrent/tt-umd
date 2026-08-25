// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "umd/device/arc/blackhole_arc_message_queue.hpp"
#include "umd/device/tt_device/firmware/blackhole_arc_apb.hpp"
#include "umd/device/tt_device/firmware/device_firmware.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {
class FirmwareTelemetryReader;
class DeviceProtocol;
class FirmwareInfoProvider;
class JtagInterface;
class PcieInterface;
class ArchitectureImplementation;

/**
 * @brief Blackhole management firmware implementation.
 */
class BlackholeDeviceFirmware : public DeviceFirmware {
public:
    BlackholeDeviceFirmware(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        ArchitectureImplementation* architecture_impl,
        FirmwareInfoProvider* firmware_info_provider,
        FirmwareTelemetryReader* firmware_telemetry_reader);

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t>& args,
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

private:
    IODeviceType get_io_device_type() const;

    // Asks the firmware to retrain one DRAM channel; throws if the firmware reports a failure.
    void retrain_dram_core(uint32_t dram_channel, NocId noc_id);

    // Polls AICLK until it is within tolerance of target_aiclk. Logs and returns if it does not
    // settle, rather than throwing.
    void wait_for_aiclk_value(uint32_t target_aiclk, std::chrono::milliseconds timeout_ms = timeout::AICLK_TIMEOUT);

    // Thin wrappers that resolve the ARC core for noc_id and hand the access to arc_apb_.
    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    void write_to_arc_apb(const void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    // All non-owning; they belong to the component that owns this firmware class and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;
    FirmwareInfoProvider* firmware_info_provider_ = nullptr;
    FirmwareTelemetryReader* firmware_telemetry_reader_ = nullptr;

    // Names the ARC message mutex; taken from the protocol so it identifies this device, not the
    // silicon model. See DeviceProtocol::get_mmio_id().
    const int device_id_ = 0;

    // ARC core coordinate per NOC, resolved once in the constructor: it depends only on the NOC
    // translation state, which is fixed for the device's lifetime.
    tt_xy_pair arc_core_noc0_;
    tt_xy_pair arc_core_noc1_;

    BlackholeArcApb arc_apb_;

    // Created by init_firmware(), not by the constructor: building it reads the queue descriptor
    // from the device, which the ARC firmware only publishes once it is up. Declared after arc_apb_
    // so it is destroyed first - it holds a raw pointer to it.
    std::unique_ptr<BlackholeArcMessageQueue> arc_msg_queue_ = nullptr;
};

}  // namespace tt::umd
