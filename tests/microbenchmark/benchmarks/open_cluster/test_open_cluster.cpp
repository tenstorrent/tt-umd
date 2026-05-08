// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
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

namespace {
constexpr const char* TT_UMD_SIMULATOR_ENV = "TT_UMD_SIMULATOR";

bool is_simulation() { return std::getenv(TT_UMD_SIMULATOR_ENV) != nullptr; }

// Mirrors TestDeviceIOFixture::make_cluster in tests/api/test_device_io.cpp:
// when TT_UMD_SIMULATOR points at a ttsim .so, switch the cluster to
// ChipType::SIMULATION so the same benchmark body runs against the simulator.
ClusterOptions make_default_options() {
    ClusterOptions options;
    if (const char* sim_path = std::getenv(TT_UMD_SIMULATOR_ENV)) {
        options.chip_type = ChipType::SIMULATION;
        options.target_devices = {0};
        options.simulator_directory = std::filesystem::path(sim_path);
    }
    return options;
}
}  // namespace

// Measure the time it takes to open/construct a Cluster object with default ClusterOptions.
TEST(MicrobenchmarkOpenCluster, ClusterConstructor) {
    ClusterOptions default_options = make_default_options();
    ClusterOptions options_with_sdesc = default_options;

    if (is_simulation()) {
        // ttsim convention: soc_descriptor.yaml lives next to libttsim.so.
        const auto& sim_dir = default_options.simulator_directory;
        options_with_sdesc.sdesc_path =
            ((sim_dir.extension() == ".so" ? sim_dir.parent_path() : sim_dir) / "soc_descriptor.yaml").string();
    } else {
        auto devices_info = PCIDevice::enumerate_devices_info();
        ASSERT_FALSE(devices_info.empty());
        tt::ARCH arch = devices_info.begin()->second.get_arch();
        options_with_sdesc.sdesc_path = test_utils::get_soc_descriptor_path(arch);
    }

    auto bench =
        ankerl::nanobench::Bench().maxEpochTime(std::chrono::seconds(30)).title("ClusterConstructor").unit("cluster");
    bench.name("default").run([&] {
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(default_options);
        ankerl::nanobench::doNotOptimizeAway(cluster);
    });
    bench.name("from sdesc").run([&] {
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options_with_sdesc);
        ankerl::nanobench::doNotOptimizeAway(cluster);
    });
    test::utils::export_results(bench);
}

TEST(MicrobenchmarkOpenCluster, TopologyDiscovery) {
    if (is_simulation()) {
        GTEST_SKIP() << "TopologyDiscovery is silicon-only; skipping under TT_UMD_SIMULATOR.";
    }
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
