// SPDX-FileCopyrightText: (c) 2023 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "umd/device/cluster.h"
#include "umd/device/lite_fabric/lite_fabric.h"

using namespace tt::umd;

// This test should be one line only.
TEST(LiteFabric, Test0) { std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(); }
