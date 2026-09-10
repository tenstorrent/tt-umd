// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>

#include "umd/device/tt_device_model/tt_device_model.hpp"
#include "umd/device/types/arch.hpp"

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

    DeviceProtocol *get_device_protocol() override;

    // Serving a PcieInterface obliges this: TTDevice runs its bus-hang check during startup
    // whenever one is present, and refuses to proceed without a detector to ask.
    HangDetector *get_hang_detector() override;

    DeviceFirmware *get_device_firmware() override;

    // Lent onward from the firmware, exactly as the silicon models do.
    FirmwareTelemetryReader *get_firmware_telemetry_reader() override;

    FirmwareInfoProvider *get_firmware_info_provider() override;

    ArchitectureImplementation *get_architecture_impl() override;

    SocArchDescriptor *get_soc_arch_descriptor() override;

    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

private:
    // Its precondition -- a protocol with a working transport -- is one only the protocol itself
    // can know it has met, so attach() is the single caller rather than this being public with a
    // condition no one outside can check.
    friend class TTSimProtocol;

    // Replace the firmware that reports nothing with the architecture's own, which reads what the
    // simulator publishes. Called only once a usable protocol is attached: an architecture firmware
    // reads the device in its constructor. A no-op for architectures that publish no firmware state.
    void use_arch_device_firmware();

    // Which architecture's firmware use_arch_device_firmware() installs.
    tt::ARCH arch_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<TTSimProtocol> tt_sim_protocol_;
    std::unique_ptr<HangDetector> hang_detector_;
    std::unique_ptr<DeviceFirmware> device_firmware_;
    // The telemetry reader and info provider belong to the concrete architecture firmwares rather
    // than to the DeviceFirmware interface, and exist only once init_firmware has run, so they are
    // captured as lookups read at call time. Empty while the firmware reports nothing.
    std::function<FirmwareTelemetryReader *()> telemetry_reader_lookup_;
    std::function<FirmwareInfoProvider *()> info_provider_lookup_;
};

}  // namespace tt::umd
