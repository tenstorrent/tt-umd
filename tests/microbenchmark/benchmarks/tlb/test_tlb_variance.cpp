// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/base.h>
#include <gtest/gtest.h>
#include <nanobench.h>

#include <cstddef>
#include <cstdint>
#include <memory>
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

void benchmark_cluster_case(
    ankerl::nanobench::Bench& bench,
    Cluster& cluster,
    const CoreCoord core,
    const uint64_t address,
    const IoOrdering ordering,
    const size_t batch_size) {
    std::vector<uint8_t> pattern(batch_size);
    bench.batch(batch_size)
        .epochIterations(1)
        .name(fmt::format("Cluster {}, write, {} bytes", ordering_name(ordering), batch_size))
        .run([&]() { cluster.write_to_device(pattern.data(), pattern.size(), CHIP_ID, core, address, ordering); });
    bench.batch(batch_size)
        .epochIterations(1)
        .name(fmt::format("Cluster {}, read, {} bytes", ordering_name(ordering), batch_size))
        .run([&]() { cluster.read_from_device(pattern.data(), CHIP_ID, core, address, batch_size, ordering); });
}

void benchmark_io_window_case(
    ankerl::nanobench::Bench& bench,
    Cluster& cluster,
    const CoreCoord core,
    const uint64_t address,
    const IoOrdering ordering,
    const size_t batch_size) {
    std::unique_ptr<IoWindow> window = cluster.create_io_window(CHIP_ID, core, address, {.size = batch_size}, ordering);
    ASSERT_NE(window, nullptr) << "Chip " << CHIP_ID << " has no device to map a window on.";

    std::vector<uint8_t> pattern(batch_size);
    bench.batch(batch_size)
        .epochIterations(1)
        .name(fmt::format("IoWindow {}, write, {} bytes", ordering_name(ordering), batch_size))
        .run([&]() { window->write_block(0, pattern.data(), pattern.size()); });
    bench.batch(batch_size)
        .epochIterations(1)
        .name(fmt::format("IoWindow {}, read, {} bytes", ordering_name(ordering), batch_size))
        .run([&]() { window->read_block(0, pattern.data(), pattern.size()); });
}

}  // namespace

// Isolated repro set for the handful of cases that showed the biggest cross-run variance in the
// full TLB sweep (test_tlb.cpp), kept separate so they can be re-run without paying for the whole
// DRAM/Tensix/Ethernet sweep each time.
TEST(MicrobenchmarkTLB, Variance) {
    auto bench = ankerl::nanobench::Bench().title("TLB_Variance").unit("byte");
    const uint64_t ADDRESS = 0x0;
    std::unique_ptr<Cluster> cluster = std::make_unique<Cluster>();
    const CoreCoord dram_core = cluster->get_soc_descriptor(CHIP_ID).get_cores(CoreType::DRAM)[0];

    benchmark_io_window_case(bench, *cluster, dram_core, ADDRESS, IoOrdering::Strict, 4 * ONE_KIB);
    benchmark_io_window_case(bench, *cluster, dram_core, ADDRESS, IoOrdering::Relaxed, 4 * ONE_KIB);
    benchmark_cluster_case(bench, *cluster, dram_core, ADDRESS, IoOrdering::Strict, 2 * ONE_KIB);
    benchmark_cluster_case(bench, *cluster, dram_core, ADDRESS, IoOrdering::Relaxed, 8 * ONE_KIB);

    test::utils::export_results(bench);
}
