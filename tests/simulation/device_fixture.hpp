// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>
#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>

#include <stdexcept>

#include "tests/test_utils/fetch_local_files.hpp"
#include "umd/device/simulation/simulation_device.hpp"

namespace tt::umd {

class SimulationDeviceFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // yaml path is dummy and won't change test behavior
        const char* simulator_path = getenv("TT_UMD_SIMULATOR");
        if (simulator_path == nullptr) {
            throw std::runtime_error(
                "You need to define TT_UMD_SIMULATOR that will point to simulator path. eg. build/versim-wormhole-b0");
        }
        device = std::make_unique<SimulationDevice>(simulator_path);
        device->start_device();
    }

    static void TearDownTestSuite() { device->close_device(); }

    static std::unique_ptr<SimulationDevice> device;
};

std::unique_ptr<SimulationDevice> SimulationDeviceFixture::device = nullptr;

}  // namespace tt::umd
