// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <gtest/gtest.h>
#include <nanobench.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/io_window/io_window.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/io_window_config.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

constexpr ChipId CHIP_ID = 0;

namespace {

const char* ordering_name(IoOrdering ordering) {
    switch (ordering) {
        case IoOrdering::Relaxed:
            return "Relaxed";
        case IoOrdering::Strict:
            return "Strict";
        case IoOrdering::Posted:
            return "Posted";
    }
    return "Unknown";
}

// Rows for the Cluster path: reconfigure per chunk, chip-wide window lock, and coordinate
// translation -- what a caller gets without managing a window itself. Called once per ordering the
// path accepts, so the pair isolates what the ordering mode costs with everything else held equal.
void benchmark_cluster(
    ankerl::nanobench::Bench& bench,
    Cluster& cluster,
    const CoreCoord core,
    const uint64_t address,
    const IoOrdering ordering,
    const std::vector<size_t>& batch_sizes) {
    for (size_t batch_size : batch_sizes) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size)
            .name(fmt::format("Cluster {}, write, {} bytes", ordering_name(ordering), batch_size))
            .run([&]() { cluster.write_to_device(pattern.data(), pattern.size(), CHIP_ID, core, address, ordering); });
    }
    for (size_t batch_size : batch_sizes) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size)
            .name(fmt::format("Cluster {}, read, {} bytes", ordering_name(ordering), batch_size))
            .run([&]() { cluster.read_from_device(pattern.data(), CHIP_ID, core, address, batch_size, ordering); });
    }
}

// Rows for a caller-owned window: mapped once and driven directly, with no lock and no reconfigure
// between transfers. Called once per ordering to isolate its cost from the Cluster path above.
void benchmark_io_window(
    ankerl::nanobench::Bench& bench,
    Cluster& cluster,
    const CoreCoord core,
    const uint64_t address,
    const IoOrdering ordering,
    const std::vector<size_t>& batch_sizes) {
    std::unique_ptr<IoWindow> window =
        cluster.create_io_window(CHIP_ID, core, address, {.size = batch_sizes.back()}, ordering);
    ASSERT_NE(window, nullptr) << "Chip " << CHIP_ID << " has no device to map a window on.";

    for (size_t batch_size : batch_sizes) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size)
            .name(fmt::format("IoWindow {}, write, {} bytes", ordering_name(ordering), batch_size))
            .run([&]() { window->write_block(0, pattern.data(), pattern.size()); });
    }
    for (size_t batch_size : batch_sizes) {
        std::vector<uint8_t> pattern(batch_size);
        bench.batch(batch_size)
            .name(fmt::format("IoWindow {}, read, {} bytes", ordering_name(ordering), batch_size))
            .run([&]() { window->read_block(0, pattern.data(), pattern.size()); });
    }
}

}  // namespace

// Measure bandwidth of IO to DRAM core.
TEST(MicrobenchmarkTLB, DRAM) {
    auto bench = ankerl::nanobench::Bench().title("TLB_DRAM").unit("byte");
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
        1 * ONE_MIB,
        2 * ONE_MIB,
        4 * ONE_MIB,
        8 * ONE_MIB,
        16 * ONE_MIB,
        32 * ONE_MIB};
    // A single window cannot carry the largest batches: at this address Wormhole tops out at its
    // 16 MB size class, and Blackhole's only class above 2 MiB is the 4 GiB one in BAR4, which is
    // too scarce to take for a benchmark. The owned-window rows stop where one window still
    // suffices, which keeps the row set identical on both architectures.
    const std::vector<size_t> WINDOW_BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB, 2 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];
    benchmark_cluster(bench, *cluster, dram_core, ADDRESS, IoOrdering::Relaxed, BATCH_SIZES);
    benchmark_cluster(bench, *cluster, dram_core, ADDRESS, IoOrdering::Strict, BATCH_SIZES);
    benchmark_io_window(bench, *cluster, dram_core, ADDRESS, IoOrdering::Relaxed, WINDOW_BATCH_SIZES);
    benchmark_io_window(bench, *cluster, dram_core, ADDRESS, IoOrdering::Strict, WINDOW_BATCH_SIZES);
    test::utils::export_results(bench);
}

// Measure bandwidth of IO to Tensix core.
TEST(MicrobenchmarkTLB, Tensix) {
    auto bench = ankerl::nanobench::Bench().title("TLB_Tensix").unit("byte");
    const uint64_t ADDRESS = 0x0;
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord tensix_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::TENSIX)[0];
    benchmark_cluster(bench, *cluster, tensix_core, ADDRESS, IoOrdering::Relaxed, BATCH_SIZES);
    benchmark_cluster(bench, *cluster, tensix_core, ADDRESS, IoOrdering::Strict, BATCH_SIZES);
    benchmark_io_window(bench, *cluster, tensix_core, ADDRESS, IoOrdering::Relaxed, BATCH_SIZES);
    benchmark_io_window(bench, *cluster, tensix_core, ADDRESS, IoOrdering::Strict, BATCH_SIZES);
    test::utils::export_results(bench);
}

// Measure bandwidth of IO to Ethernet core.
TEST(MicrobenchmarkTLB, Ethernet) {
    auto bench = ankerl::nanobench::Bench().title("TLB_Ethernet").unit("byte");
    const uint64_t ADDRESS = 0x20000;  // 128 KiB
    const std::vector<size_t> BATCH_SIZES = {
        1, 2, 4, 8, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 128 * ONE_KIB};
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    if (cluster->get_soc_descriptor(CHIP_ID).get_num_eth_channels() == 0) {
        GTEST_SKIP() << "No ETH cores found on system.";
    }
    const CoreCoord eth_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::ETH).at(0);
    benchmark_cluster(bench, *cluster, eth_core, ADDRESS, IoOrdering::Relaxed, BATCH_SIZES);
    benchmark_cluster(bench, *cluster, eth_core, ADDRESS, IoOrdering::Strict, BATCH_SIZES);
    benchmark_io_window(bench, *cluster, eth_core, ADDRESS, IoOrdering::Relaxed, BATCH_SIZES);
    benchmark_io_window(bench, *cluster, eth_core, ADDRESS, IoOrdering::Strict, BATCH_SIZES);
    test::utils::export_results(bench);
}

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
                for (auto& tensix_core : tensix_cores) {
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
