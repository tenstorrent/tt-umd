// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "umd/device/tt_device_model/tt_device_model.hpp"

namespace tt::umd {

// Model for a simulated device. A simulation backend is reached in-process rather than over a host
// transport, and takes its architecture from the SoC descriptor it is built with rather than from a
// probed device.
// TODO: split into one model per backend (RTL simulation, TTSim) once they wire components that
// actually differ between them.
class SimulationTTDeviceModel : public TTDeviceModel {
public:
    explicit SimulationTTDeviceModel(tt::ARCH arch);

    tt::ARCH get_arch() const override;

    int get_communication_device_id() const override;

private:
    tt::ARCH arch_;
};

}  // namespace tt::umd
