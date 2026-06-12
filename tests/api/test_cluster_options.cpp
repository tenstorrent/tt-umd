// SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <type_traits>

#include "umd/device/cluster.hpp"
#include "umd/device/types/cluster_types.hpp"

using namespace tt::umd;

// Verifies the documented defaults of a freshly constructed ClusterOptions{}.
TEST(ClusterOptions, Defaults) {
    ClusterOptions opts{};

    // Host memory channels default to nullopt so the auto-calculate path runs in construct_cluster.
    EXPECT_EQ(opts.num_host_mem_ch_per_mmio_device, std::nullopt);
    EXPECT_EQ(opts.chip_type, ChipType::SILICON);
    EXPECT_TRUE(opts.target_devices.empty());

    // An explicit 0 is distinct from nullopt and must be preserved.
    ClusterOptions explicit_zero{.num_host_mem_ch_per_mmio_device = 0};
    ASSERT_TRUE(explicit_zero.num_host_mem_ch_per_mmio_device.has_value());
    EXPECT_EQ(explicit_zero.num_host_mem_ch_per_mmio_device.value(), 0u);
}

// Cluster(ClusterOptions) must be explicit to prevent implicit conversions at call sites.
TEST(ClusterTest, ConstructorIsExplicit) {
    // Explicit construction from ClusterOptions must be allowed.
    EXPECT_TRUE((std::is_constructible_v<Cluster, ClusterOptions>))
        << "Cluster should be explicitly constructible from ClusterOptions.";

    // Implicit conversion from ClusterOptions must be blocked.
    EXPECT_FALSE((std::is_convertible_v<ClusterOptions, Cluster>))
        << "API Violation: Cluster(ClusterOptions) must be explicit to prevent implicit conversion!";
}
