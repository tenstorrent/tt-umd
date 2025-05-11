// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <random>

#include "tests/test_utils/generate_cluster_desc.hpp"
#include "umd/device/cluster.h"
#include "umd/device/tt_soc_descriptor.h"

class uBenchmarkFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // get arch name?
        results_csv.open("ubench_results.csv", std::ios_base::app);

        auto get_static_tlb_index = [](tt_xy_pair target) {
            int flat_index = target.y * 10 + target.x;  // grid_size_x = 10 for GS/WH ????? something is wrong here
            if (flat_index == 0) {
                return -1;
            }
            return flat_index;
        };
        device = std::make_shared<tt::umd::Cluster>(tt::umd::ClusterOptions{.target_devices = {0}});
    }

    void TearDown() override {
        device->close_device();
        results_csv.close();
    }

    std::shared_ptr<tt::umd::Cluster> device;
    std::ofstream results_csv;
};
