// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/simulation_tt_device_model.hpp"

namespace tt::umd {

// A simulation backend has no host transport to be addressed within, so it reports no communication
// device at all.
SimulationTTDeviceModel::SimulationTTDeviceModel(tt::ARCH arch) : TTDeviceModel(arch, IODeviceType::UNDEFINED, -1) {}

}  // namespace tt::umd
