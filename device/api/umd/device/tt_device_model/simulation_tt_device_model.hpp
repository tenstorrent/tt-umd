// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "umd/device/tt_device_model/tt_device_model.hpp"
#include "umd/device/types/arch.hpp"

namespace tt::umd {
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

    DeviceFirmware *get_device_firmware() override;

    ArchitectureImplementation *get_architecture_impl() override;

    SocArchDescriptor *get_soc_arch_descriptor() override;

    std::shared_ptr<SocArchDescriptor> get_shared_soc_arch_descriptor() override;

private:
    std::unique_ptr<ArchitectureImplementation> architecture_impl_;
    std::unique_ptr<SimulationDeviceFirmware> device_firmware_;
};

}  // namespace tt::umd
