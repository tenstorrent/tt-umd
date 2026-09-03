// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

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

    DeviceFirmware *get_device_firmware() override;

    ArchitectureImplementation *get_architecture_impl() override;

    SocArchDescriptor *get_soc_arch_descriptor() override;

    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

private:
    tt::ARCH arch_;
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<TTSimProtocol> tt_sim_protocol_;
    std::unique_ptr<HangDetector> hang_detector_;
    std::unique_ptr<SimulationDeviceFirmware> device_firmware_;
};

}  // namespace tt::umd
