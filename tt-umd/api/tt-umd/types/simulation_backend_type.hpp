// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace tt::umd {

// Which simulator backs a simulated device; mirrors wire::SimulationBackendType, as a simulation host serves it to its
// clients as part of the device identity.
enum class SimulationBackendType : int8_t {
    TTSIM = 0,
    RTL = 1,
};

}  // namespace tt::umd
