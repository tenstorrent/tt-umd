// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "utils.hpp"

using namespace tt::umd;
using namespace tt::umd::microbenchmarks;

class Multicast : public ClusterBenchmark {};

// Writing the same data to every Tensix core: one multicast against one write per core. The batch is the number of
// bytes that land on the chip, so both methods are compared at the same amount of delivered data.
TEST_F(Multicast, Write) {
    const std::vector<CoreCoord> tensix_cores = cluster->get_soc_descriptor(mmio_chip).get_cores(tt::CoreType::TENSIX);
    const uint64_t address = 0x1000;
    const std::vector<size_t> payload_sizes = {4, 64, 1 * ONE_KIB, 16 * ONE_KIB};
    std::vector<uint8_t> data(payload_sizes.back());

    const std::vector<std::string> context_variables = {"method", "payload"};
    Bench bench;
    bench.title("Multicast").unit("byte").performanceCounters(false).warmup(1);

    auto benchmark = [&](const std::string& method, auto write_to_all_cores) {
        for (const size_t payload_size : payload_sizes) {
            const std::string payload = format_with_unit(static_cast<double>(payload_size), "byte");
            bench.context("method", method).context("payload", payload).batch(payload_size * tensix_cores.size());
            bench.run(
                fmt::format("method: {}, payload: {}", method, payload), [&] { write_to_all_cores(payload_size); });
        }
    };

    benchmark("multicast", [&](size_t payload_size) {
        cluster->noc_multicast_write(
            data.data(), payload_size, mmio_chip, tensix_cores.front(), tensix_cores.back(), address);
    });
    benchmark("write_per_core", [&](size_t payload_size) {
        for (const CoreCoord& core : tensix_cores) {
            cluster->write_to_device(data.data(), payload_size, mmio_chip, core, address);
        }
    });
    export_results(bench, context_variables);
}
