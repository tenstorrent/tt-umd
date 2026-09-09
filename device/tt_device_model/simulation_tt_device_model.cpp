// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"

#include "umd/device/arch/architecture_implementation.hpp"
#include "umd/device/tt_device/firmware/simulation_device_firmware.hpp"

namespace tt::umd {

SimulationTTDeviceModel::SimulationTTDeviceModel(tt::ARCH arch) :
    architecture_impl_(ArchitectureImplementation::create(arch)),
    device_firmware_(std::make_unique<SimulationDeviceFirmware>(arch)) {}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
SimulationTTDeviceModel::~SimulationTTDeviceModel() = default;

// A simulation backend reaches its device directly rather than over a transport protocol; the
// simulation TTDevice overrides the read/write paths instead of routing them through one.
DeviceProtocol *SimulationTTDeviceModel::get_device_protocol() { return nullptr; }

DeviceFirmware *SimulationTTDeviceModel::get_device_firmware() { return device_firmware_.get(); }

ArchitectureImplementation *SimulationTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

// A simulation backend supplies a full SocDescriptor of its own rather than an architecture
// descriptor for TTDevice to build one from.
SocArchDescriptor *SimulationTTDeviceModel::get_soc_arch_descriptor() { return nullptr; }

std::shared_ptr<SocArchDescriptor> SimulationTTDeviceModel::get_shared_soc_arch_descriptor() { return nullptr; }

}  // namespace tt::umd
