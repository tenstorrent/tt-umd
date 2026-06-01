// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>

#include "umd/device/cluster.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

TEST(ClusterOptions, DefaultHasNulloptHostMemChannels) {
    ClusterOptions opts{};
    EXPECT_EQ(opts.num_host_mem_ch_per_mmio_device, std::nullopt);
}

TEST(ClusterOptions, DefaultIsSilicon) {
    ClusterOptions opts{};
    EXPECT_EQ(opts.chip_type, ChipType::SILICON);
}

TEST(ClusterOptions, DefaultHasEmptyTargetDevices) {
    ClusterOptions opts{};
    EXPECT_TRUE(opts.target_devices.empty());
}

TEST(ClusterOptions, ExplicitZeroHostMemChannelsIsNotNullopt) {
    ClusterOptions opts{.num_host_mem_ch_per_mmio_device = 0};
    ASSERT_TRUE(opts.num_host_mem_ch_per_mmio_device.has_value());
    EXPECT_EQ(opts.num_host_mem_ch_per_mmio_device.value(), 0u);
}

TEST(ClusterOptions, ExplicitConstructorCompiles) {
    try {
        Cluster c(ClusterOptions{});
        (void)c;
    } catch (const std::exception&) {
        SUCCEED();
    }
}
