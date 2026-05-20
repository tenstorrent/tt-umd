// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "common/microbenchmark_utils.hpp"
#include "test_utils/fetch_local_files.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/topology/topology_discovery.hpp"
#include "umd/device/types/communication_protocol.hpp"

namespace tt {
enum class ARCH;
}  // namespace tt

using namespace tt::umd;
using namespace tt::umd::test::utils;

// Measure the time it takes to open/construct a Cluster object with default ClusterOptions.
TEST(MicrobenchmarkOpenCluster, ClusterConstructor) {
    auto devices_info = PCIDevice::enumerate_devices_info();
    ASSERT_FALSE(devices_info.empty());
    tt::ARCH arch = devices_info.begin()->second.get_arch();
    ClusterOptions options;
    options.sdesc_path = test_utils::get_soc_descriptor_path(arch);

    auto bench = ankerl::nanobench::Bench()
                     .maxEpochTime(std::chrono::seconds(30))
                     .epochs(100)
                     .title("ClusterConstructor")
                     .unit("cluster")
                     .minEpochIterations(10);
    bench.name("default").run([&] {
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
        ankerl::nanobench::doNotOptimizeAway(cluster);
    });
    bench.name("from sdesc").run([&] {
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options);
        ankerl::nanobench::doNotOptimizeAway(cluster);
    });
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkOpenCluster, TopologyDiscovery) {
    auto bench =
        ankerl::nanobench::Bench().maxEpochTime(std::chrono::seconds(30)).title("TopologyDiscovery").unit("discovery");
    bench.name("default").run([&] {
        auto [cluster_descriptor, devices] = TopologyDiscovery::discover({});
        ankerl::nanobench::doNotOptimizeAway(devices);
    });
    bench.name("local only").run([&] {
        auto [cluster_descriptor, devices] = TopologyDiscovery::discover({.discover_remote_devices = false});
        ankerl::nanobench::doNotOptimizeAway(devices);
    });

    auto devices_info = PCIDevice::enumerate_devices_info();
    ASSERT_FALSE(devices_info.empty());
    tt::ARCH arch = devices_info.begin()->second.get_arch();
    std::string sdesc_path = test_utils::get_soc_descriptor_path(arch);

    bench.name("default with sdesc_path").run([&] {
        auto [cluster_descriptor, devices] = TopologyDiscovery::discover({}, IODeviceType::PCIe, sdesc_path);
        ankerl::nanobench::doNotOptimizeAway(devices);
    });
    bench.name("local only with sdesc_path").run([&] {
        auto [cluster_descriptor, devices] =
            TopologyDiscovery::discover({.discover_remote_devices = false}, IODeviceType::PCIe, sdesc_path);
        ankerl::nanobench::doNotOptimizeAway(devices);
    });

    test::utils::export_results(bench);
}
