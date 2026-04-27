// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "tt-umd-workload/cluster.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

// Since multicast has multiple endpoints as targets, it's not compeletely fair to compare
// the bandwidth, which is still tied to TLB bandwidth. BW of multicast writes will be the same in terms
// of BW as unicast writes. The benefit of multicast is in saving time by writing to multiple endpoints in one go.
// However, it is interesting to see the time taken for unicast vs multicast writes to multiple endpoints.
// That is why this test is disabled by default. It's meant for someone to run it manually if needed.
TEST(MicrobenchmarkTLB, CompareMulticastandUnicast) {
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1,
        2,
        4,
        8,
        1 * ONE_KIB,
        2 * ONE_KIB,
        4 * ONE_KIB,
        8 * ONE_KIB,
        16 * ONE_KIB,
        32 * ONE_KIB,
        64 * ONE_KIB,
        128 * ONE_KIB,
        256 * ONE_KIB,
        512 * ONE_KIB,
        1 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    std::vector<Result> results;
    auto tensix_cores = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX);
    for (size_t batch_size : BATCH_SIZES) {
        auto bench = ankerl::nanobench::Bench().title("TLB_Tensix_Unicast_v_Multicast").unit("byte");
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size)
            .name(fmt::format("Unicast, {} cores, {} bytes", tensix_cores.size(), batch_size))
            .relative(true)
            .run([&]() {
                for (auto &tensix_core : tensix_cores) {
                    cluster->write_to_device(pattern.data(), pattern.size(), CHIP_ID, tensix_core, ADDRESS);
                }
            });
        bench.batch(batch_size)
            .name(fmt::format("Multicast, {} cores, {} bytes", tensix_cores.size(), batch_size))
            .run([&]() {
                cluster->noc_multicast_write(
                    pattern.data(), pattern.size(), CHIP_ID, tensix_cores.front(), tensix_cores.back(), ADDRESS);
            });

        results.reserve(results.size() + bench.results().size());
        results.insert(results.end(), bench.results().begin(), bench.results().end());
    }
    export_results("TLB_Tensix_Unicast_v_Multicast", results);
}
