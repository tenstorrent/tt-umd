// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "utils.hpp"

using namespace tt::umd;
using namespace tt::umd::microbenchmarks;

class RemoteIO : public ClusterBenchmark {};

// IO with a chip that has no PCIe link of its own and is reached over Ethernet through an MMIO capable chip, such as
// the second chip of an n300.
TEST_F(RemoteIO, IOSpeed) {
    const std::set<tt::ChipId> remote_chips = cluster->get_target_remote_device_ids();
    if (remote_chips.empty()) {
        GTEST_SKIP() << "No remote chips found in the cluster.";
    }
    const tt::ChipId remote_chip = *remote_chips.begin();
    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(remote_chip);

    struct Target {
        CoreCoord core;
        uint64_t address;
    };

    const std::vector<Target> targets = {
        {soc_desc.get_cores(tt::CoreType::TENSIX).front(), 0x1000},
        {soc_desc.get_cores(tt::CoreType::DRAM).front(), 0x0},
    };
    const std::vector<size_t> batch_sizes = {4, 8, 16, 64, 256, 1 * ONE_KIB, 4 * ONE_KIB, 16 * ONE_KIB, 64 * ONE_KIB};
    std::vector<uint8_t> data(batch_sizes.back());

    const std::vector<std::string> context_variables = {"direction", "core", "coretype"};
    Bench bench;
    bench.title("RemoteIO").unit("byte").performanceCounters(false).warmup(1);

    auto benchmark = [&](const std::string& direction, auto operation) {
        for (const Target& target : targets) {
            for (const size_t batch_size : batch_sizes) {
                const std::string core = target.core.str();
                const std::string core_type = to_str(target.core.core_type);
                bench.context("direction", direction)
                    .context("core", core)
                    .context("coretype", core_type)
                    .batch(batch_size);
                const std::string name = fmt::format(
                    "direction: {}, core: {}, coretype: {}, batch: {}",
                    direction,
                    core,
                    core_type,
                    format_with_unit(batch_size, "byte"));
                bench.run(name, [&] { operation(target, batch_size); });
            }
        }
    };

    benchmark("read", [&](const Target& target, size_t batch_size) {
        cluster->read_from_device(data.data(), remote_chip, target.core, target.address, batch_size);
    });
    benchmark("write", [&](const Target& target, size_t batch_size) {
        cluster->write_to_device(data.data(), batch_size, remote_chip, target.core, target.address);
        // Remote writes are queued, so the write is only done once the queue is flushed.
        cluster->wait_for_non_mmio_flush(remote_chip);
    });
    export_results(bench, context_variables);
}
