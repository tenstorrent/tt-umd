// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"

namespace tt::umd {

SimulationTTDeviceModel::SimulationTTDeviceModel(tt::ARCH arch) : arch_(arch) {}

tt::ARCH SimulationTTDeviceModel::get_arch() const { return arch_; }

// A simulation backend has no host transport to be addressed within, so it reports no communication
// device at all.
int SimulationTTDeviceModel::get_communication_device_id() const { return -1; }

// A simulation backend reaches its device directly rather than over a transport protocol; the
// simulation TTDevice overrides the read/write paths instead of routing them through one.
DeviceProtocol *SimulationTTDeviceModel::get_device_protocol() { return nullptr; }

}  // namespace tt::umd
