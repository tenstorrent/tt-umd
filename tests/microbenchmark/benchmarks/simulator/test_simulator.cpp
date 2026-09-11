// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

// Microbenchmarks that run against the TTSim functional simulator (see tenstorrent/ttsim).

#include <fmt/base.h>
#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "tests/test_utils/device_test_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

namespace {

constexpr ChipId CHIP_ID = 0;
constexpr uint64_t ADDRESS = 0x0;
constexpr size_t TRANSFER_SIZE = 4 * ONE_KIB;

class MicrobenchmarkSim : public ::testing::Test {
protected:
    void SetUp() override {
        sim_path_ = std::getenv("TT_UMD_SIMULATOR");
        if (sim_path_ == nullptr) {
            GTEST_SKIP() << "TT_UMD_SIMULATOR not set; skipping simulator microbenchmark.";
        }
    }

    const char* sim_path_ = nullptr;
};

}  // namespace

TEST_F(MicrobenchmarkSim, ClusterConstructor) {
    ClusterOptions options =
        test_utils::get_default_sim_cluster_options(sim_path_, /*num_host_mem_ch_per_mmio_device=*/0);
    auto bench = ankerl::nanobench::Bench()
                     .epochs(100)
                     .maxEpochTime(std::chrono::seconds(30))
                     .title("ClusterConstructorSim")
                     .unit("cluster");
    bench.name("default").run([&] {
        std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(options);
        ankerl::nanobench::doNotOptimizeAway(cluster);
    });
    test::utils::export_results(bench);
}

TEST_F(MicrobenchmarkSim, DeviceIO) {
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>(
        test_utils::get_default_sim_cluster_options(sim_path_, /*num_host_mem_ch_per_mmio_device=*/0));
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX).at(0);

    std::vector<uint8_t> pattern(TRANSFER_SIZE, 0xAB);
    std::vector<uint8_t> readback(TRANSFER_SIZE);
    auto bench = ankerl::nanobench::Bench().title("DeviceIOSim").unit("byte").epochs(50);
    bench.batch(TRANSFER_SIZE).name(fmt::format("write, {} bytes", TRANSFER_SIZE)).run([&] {
        cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
    });
    bench.batch(TRANSFER_SIZE).name(fmt::format("read, {} bytes", TRANSFER_SIZE)).run([&] {
        cluster->read_from_device(readback.data(), CHIP_ID, tensix_core, ADDRESS, TRANSFER_SIZE);
    });
    test::utils::export_results(bench);
}
