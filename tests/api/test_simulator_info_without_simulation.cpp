// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// The public simulator queries in a build of UMD without simulation support.

#include <gtest/gtest.h>

#include "tests/test_utils/simulator_layout.hpp"
#include "tt-umd/simulation/simulator_info.hpp"
#include "tt-umd/utils/error.hpp"

using namespace tt::umd;
using tt::umd::test_utils::SimulatorLayout;

// Even a simulator that ships its soc_descriptor.yaml is refused: this build cannot run any simulator.
TEST(SimulatorInfo, SimulatorArchNeedsSimulationSupport) {
    const SimulatorLayout simulator;
    simulator.add_soc_descriptor("quasar_32_arch.yaml");

    EXPECT_THROW(get_simulator_arch(simulator.add_ttsim_library()), error::UmdException<error::RuntimeError>);
}
