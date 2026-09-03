// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>

#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

class TTSimProtocol;
class SimulationDeviceFirmware;

// Model for a simulated device. A simulation backend is reached in-process rather than over a host
// transport, and takes its architecture from the SoC descriptor it is built with rather than from a
// probed device.
// TODO: split into one model per backend (RTL simulation, TTSim) once they wire components that
// actually differ between them.
class SimulationTTDeviceModel : public TTDeviceModel {
public:
    explicit SimulationTTDeviceModel(tt::ARCH arch);

    ~SimulationTTDeviceModel() override;

    tt::ARCH get_arch() const override;

    int get_communication_device_id() const override;

    DeviceProtocol *get_device_protocol() override;

    // Serving a PcieInterface obliges this: TTDevice runs its bus-hang check during startup
    // whenever one is present, and refuses to proceed without a detector to ask.
    HangDetector *get_hang_detector() override;

    // Replace the firmware that reports nothing with the architecture's own, which reads what the
    // simulator publishes. Called by a device that has attached a usable protocol, and only then: an
    // architecture firmware reads the device in its constructor -- Blackhole resolves its ARC
    // coordinates from NOC translation state over BAR -- so it cannot exist before the transport
    // works. A no-op for architectures a simulator publishes no firmware state for.
    void use_arch_device_firmware();

    DeviceFirmware *get_device_firmware() override;

    // Lent onward from the firmware, exactly as the silicon models do: whatever it built while
    // reading the device is what callers asking this model should see.
    FirmwareTelemetryReader *get_firmware_telemetry_reader() override;

    FirmwareInfoProvider *get_firmware_info_provider() override;

    ArchitectureImplementation *get_architecture_impl() override;

    SocArchDescriptor *get_soc_arch_descriptor() override;

    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

private:
    tt::ARCH arch_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<TTSimProtocol> tt_sim_protocol_;
    std::unique_ptr<HangDetector> hang_detector_;
    std::unique_ptr<DeviceFirmware> device_firmware_;
    // The telemetry reader and info provider are accessors of the concrete architecture firmwares
    // rather than of the DeviceFirmware interface, and they only exist once init_firmware has run.
    // Captured as lookups when the firmware is installed, so they are read at call time rather than
    // sampled too early. Empty while the firmware reports nothing, which has neither.
    std::function<FirmwareTelemetryReader *()> telemetry_reader_lookup_;
    std::function<FirmwareInfoProvider *()> info_provider_lookup_;
};

}  // namespace tt::umd
