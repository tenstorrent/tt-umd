// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"

#include "umd/device/arch/architecture_implementation.hpp"

namespace tt::umd {

SimulationTTDeviceModel::SimulationTTDeviceModel(tt::ARCH arch) :
    arch_(arch), architecture_impl_(ArchitectureImplementation::create(arch)) {}

// Out-of-line: the unique_ptr members hold forward-declared types, whose deleters need a
// complete type where the destructor is instantiated.
SimulationTTDeviceModel::~SimulationTTDeviceModel() = default;

tt::ARCH SimulationTTDeviceModel::get_arch() const { return arch_; }

// A simulation backend has no host transport to be addressed within, so it reports no communication
// device at all.
int SimulationTTDeviceModel::get_communication_device_id() const { return -1; }

// A simulation backend reaches its device directly rather than over a transport protocol; the
// simulation TTDevice overrides the read/write paths instead of routing them through one.
DeviceProtocol *SimulationTTDeviceModel::get_device_protocol() { return nullptr; }

ArchitectureImplementation *SimulationTTDeviceModel::get_architecture_impl() { return architecture_impl_.get(); }

// A simulation backend supplies a full SocDescriptor of its own rather than an architecture
// descriptor for TTDevice to build one from.
SocArchDescriptor *SimulationTTDeviceModel::get_soc_arch_descriptor() { return nullptr; }

std::shared_ptr<SocArchDescriptor> SimulationTTDeviceModel::get_shared_soc_arch_descriptor() { return nullptr; }

}  // namespace tt::umd
