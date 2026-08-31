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
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/lock_manager.hpp"
#include "umd/device/utils/timeouts.hpp"

namespace tt::umd {

class ArchitectureImplementation;
class DeviceProtocol;
class FirmwareInfoProvider;
class FirmwareTelemetryReader;
class JtagInterface;
class PcieInterface;

/**
 * @brief Blackhole implementation of DeviceFirmware.
 *
 * Built from protocol interfaces: the device protocol it issues NOC accesses through, exactly one of
 * the optional PCIe/JTAG transports (which one is present is how routes are picked), and the
 * architecture implementation for register layout. All non-owning; they belong to the object that
 * owns this one and must outlive it.
 */
class BlackholeDeviceFirmware : public DeviceFirmware {
public:
    BlackholeDeviceFirmware(
        DeviceProtocol* device_protocol,
        PcieInterface* pcie_interface,
        JtagInterface* jtag_interface,
        ArchitectureImplementation* architecture_impl);

    // Defined in the .cpp, where the owned components' types are complete.
    ~BlackholeDeviceFirmware() override;

    void init_firmware(std::chrono::milliseconds timeout_ms, NocId noc_id = NocId::DEFAULT_NOC) override;

    DeviceCommandResult send_device_command(
        uint32_t msg_code,
        const std::vector<uint32_t>& args,
        std::chrono::milliseconds timeout,
        NocId noc_id = NocId::DEFAULT_NOC) override;

    void set_clock_state(ClockState state, NocId noc_id = NocId::DEFAULT_NOC) override;

    /**
     * @brief Telemetry published by the management firmware.
     *
     * Owned here: it reads state the firmware publishes, so it cannot exist until init_firmware()
     * has brought the firmware up. Deliberately a concrete-class getter rather than part of
     * DeviceFirmware: the concrete model lends it onward, and the facade turns a null into
     * UninitializedDeviceError carrying the device's identity.
     *
     * @return The reader, or nullptr until init_firmware() has run.
     */
    FirmwareTelemetryReader* get_firmware_telemetry_reader() const;

    /**
     * @brief Firmware-reported device information. Owned and lent the same way as the telemetry
     * reader.
     *
     * @return The provider, or nullptr until init_firmware() has run.
     */
    FirmwareInfoProvider* get_firmware_info_provider() const;

private:
    /**
     * @brief Blocks until the management firmware reports it has booted.
     *
     * Kept as its own step because init_firmware() has a second job - building the components
     * that read what the firmware publishes - and callers such as warm reset only want this first
     * one. Once the two become separate API calls, init_firmware()'s idempotence guard goes away
     * with them.
     *
     * @param timeout_ms How long to wait for the firmware to report ready.
     * @param noc_id NOC to route the status reads over.
     * @throws error::FirmwareStartupError if the firmware does not come up within the timeout.
     */
    void wait_firmware_ready(std::chrono::milliseconds timeout_ms, NocId noc_id);

    IODeviceType get_io_device_type() const;

    // Polls AICLK until it is within tolerance of target_aiclk. Logs and returns if it does not
    // settle, rather than throwing.
    void wait_for_aiclk_value(uint32_t target_aiclk, std::chrono::milliseconds timeout_ms = timeout::AICLK_TIMEOUT);

    // Whether NOC address translation is active. Private for now: the constructor needs it to
    // resolve the ARC coordinates, while TTDevice::get_noc_translation_enabled stays the public
    // entry until that API moves here. Read over BAR/JTAG, so it does not need the firmware up.
    bool get_noc_translation_enabled() const;

    // The management firmware core's coordinate, resolved for noc_id. Private until the TTDevice
    // API it replaces moves here.
    tt_xy_pair get_firmware_noc_coord(NocId noc_id) const;

    // Thin wrapper that resolves the ARC core for noc_id and hands the access to arc_apb_.
    void read_from_arc_apb(void* mem_ptr, uint64_t arc_addr_offset, size_t size, NocId noc_id);

    // All non-owning; they belong to the object that owns this one and must outlive it.
    DeviceProtocol* device_protocol_ = nullptr;
    PcieInterface* pcie_interface_ = nullptr;
    JtagInterface* jtag_interface_ = nullptr;
    ArchitectureImplementation* architecture_impl_ = nullptr;

    // Identifies this device in error payloads; taken from the protocol so it identifies the
    // device, not the silicon model. See DeviceProtocol::get_mmio_id().
    int device_id_ = 0;

    // ARC core coordinate per NOC, resolved once in the constructor: it depends only on the NOC
    // translation state, which is fixed for the device's lifetime.
    tt_xy_pair arc_core_noc0_;
    tt_xy_pair arc_core_noc1_;

    BlackholeArcApb arc_apb_;

    // Serializes ARC messages against other processes driving the same device, exactly as
    // ArcMessenger does for the path this replaces.
    LockManager lock_manager_;

    // Created by init_firmware(), not the constructor: building it reads the queue descriptor
    // from the device, which the ARC firmware only publishes once it is up. Declared after arc_apb_
    // so it is destroyed first - it holds a raw pointer to it.
    std::unique_ptr<BlackholeArcMessageQueue> arc_msg_queue_;

    // Created by init_firmware(), not the constructor: they read state the firmware publishes, so
    // this is the earliest they can exist. The info provider holds a raw pointer to the telemetry
    // reader, so it is declared after it and destroyed first.
    std::unique_ptr<FirmwareTelemetryReader> firmware_telemetry_reader_;
    std::unique_ptr<FirmwareInfoProvider> firmware_info_provider_;
};

}  // namespace tt::umd
