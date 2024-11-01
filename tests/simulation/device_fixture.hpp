// SPDX-FileCopyrightText: © 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>

#include "tt_simulation_device.h"
#include "common/logger.hpp"
#include "tests/test_utils/generate_cluster_desc.hpp"

#include <nng/nng.h>
#include <nng/protocol/pipeline0/pull.h>
#include <nng/protocol/pipeline0/push.h>
#include <nng/protocol/pair1/pair.h>

class SimulationDeviceFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // default_params and yaml path are both dummy and won't change test behavior
        tt_device_params default_params;
        device = std::make_unique<tt_SimulationDevice>(test_utils::GetAbsPath("tests/soc_descs/grayskull_10x12.yaml"));
        device->start_device(default_params);
    }

    static void TearDownTestSuite() {
        device->close_device();
    }

    static std::unique_ptr<tt_SimulationDevice> device;
};

std::unique_ptr<tt_SimulationDevice> SimulationDeviceFixture::device = nullptr;
