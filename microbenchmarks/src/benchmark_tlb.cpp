// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/io_window/io_window.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "utils.hpp"

using namespace tt::umd;
using namespace tt::umd::microbenchmarks;

class TLB : public ::testing::Test {
protected:
    void SetUp() override {
        cluster = std::make_unique<Cluster>(ClusterOptions{});
        if (cluster->get_target_device_ids().empty()) {
            GTEST_SKIP() << "No devices found in the cluster.";
        }
        DEVICE = *cluster->get_target_mmio_device_ids().begin();
        const SocDescriptor& soc_desc = cluster->get_soc_descriptor(DEVICE);
        TENSIX.core = soc_desc.get_cores(tt::CoreType::TENSIX).front();
        TENSIX.address = 0x1000;
        TENSIX.window_size = 1 * ONE_MIB;
        TENSIX.batch_sizes = {1,  2,  3,   4,   5,   6,           7,           8,           12,          16,
                              32, 64, 128, 256, 512, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 1 * ONE_MIB};

        DRAM.core = soc_desc.get_cores(tt::CoreType::DRAM).front();
        DRAM.address = 0x0;
        DRAM.window_size = 1 * ONE_MIB;
        DRAM.batch_sizes = {1,           2,           3,           4,           5,           6,
                            7,           8,           12,          16,          32,          64,
                            128,         256,         512,         1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB,
                            8 * ONE_KIB, 1 * ONE_MIB, 2 * ONE_MIB, 4 * ONE_MIB, 8 * ONE_MIB, 16 * ONE_MIB,
                            32 * ONE_MIB};

        ETH.core = soc_desc.get_cores(tt::CoreType::ETH).front();
        ETH.address = 0x20000;  // 128 KiB from 0
        ETH.window_size = 128 * ONE_KIB;
        ETH.batch_sizes = {1,  2,  3,   4,   5,   6,           7,           8,           12,          16,
                           32, 64, 128, 256, 512, 1 * ONE_KIB, 2 * ONE_KIB, 4 * ONE_KIB, 8 * ONE_KIB, 128 * ONE_KIB};
    }

    std::unique_ptr<Cluster> cluster;
    tt::ChipId DEVICE = 0;

    // A core to benchmark, the address to access on it, the IoWindow size and the batch sizes to sweep. They are filled
    // in SetUp(). Batch sizes above window_size are only benchmarked through the cluster API, which is not bound by a
    // single window. Tensix and ETH sweeps stop at their window size, since their L1 has no room for larger batches.
    struct Target {
        CoreCoord core;
        uint64_t address;
        size_t window_size;
        std::vector<size_t> batch_sizes;
    };

    Target TENSIX;
    Target DRAM;
    Target ETH;
};

TEST_F(TLB, IOSpeed) {
    const std::vector<Target> targets = {TENSIX, DRAM, ETH};
    const std::vector<std::string> context_variables = {"api_level", "direction", "core", "coretype", "ordering"};
    const std::vector<IoOrdering> orderings = {IoOrdering::Relaxed, IoOrdering::Strict};
    std::vector<uint8_t> data(DRAM.batch_sizes.back());

    Bench bench;
    bench.title("TLB").unit("byte").performanceCounters(false).warmup(1);

    // make_operation(target, ordering) returns a callable taking the batch size. It is built per target and ordering,
    // outside the timed region, so the timed loop contains no api level or direction branch.
    auto benchmark = [&](const std::string& api_level,
                         const std::string& direction,
                         const bool fit_window_only,
                         auto make_operation) {
        for (const Target& target : targets) {
            for (const IoOrdering ordering : orderings) {
                auto operation = make_operation(target, ordering);
                for (const size_t batch_size : target.batch_sizes) {
                    if (fit_window_only && batch_size > target.window_size) {
                        continue;
                    }
                    const std::string core = target.core.str();
                    const std::string core_type = to_str(target.core.core_type);
                    const std::string ordering_name = to_str(ordering);
                    bench.context("api_level", api_level)
                        .context("direction", direction)
                        .context("core", core)
                        .context("coretype", core_type)
                        .context("ordering", ordering_name)
                        .batch(batch_size);

                    const std::string name = fmt::format(
                        "api_level: {}, direction: {}, core: {}, coretype: {}, ordering: {}, batch: {}",
                        api_level,
                        direction,
                        core,
                        core_type,
                        ordering_name,
                        format_with_unit(batch_size, "byte"));
                    bench.run(name, [&] { operation(batch_size); });
                }
            }
        }
    };

    // An IoWindow's ordering is fixed at creation, so every target and ordering gets its own window.
    auto make_window = [&](const Target& target, IoOrdering ordering) {
        return std::shared_ptr<IoWindow>(cluster->create_io_window(
            DEVICE,
            target.core,
            target.address,
            HostIoWindowConfig{HostMemoryCaching::WC, target.window_size},
            ordering));
    };

    benchmark("cluster", "read", false, [&](const Target& target, IoOrdering ordering) {
        return [&, core = target.core, address = target.address, ordering](size_t batch_size) {
            cluster->read_from_device(data.data(), DEVICE, core, address, batch_size, ordering);
        };
    });
    benchmark("cluster", "write", false, [&](const Target& target, IoOrdering ordering) {
        return [&, core = target.core, address = target.address, ordering](size_t batch_size) {
            cluster->write_to_device(data.data(), batch_size, DEVICE, core, address, ordering);
        };
    });
    benchmark("iowindow", "read", true, [&](const Target& target, IoOrdering ordering) {
        return [&, window = make_window(target, ordering)](size_t batch_size) {
            window->read_block(0, data.data(), batch_size);
        };
    });
    benchmark("iowindow", "write", true, [&](const Target& target, IoOrdering ordering) {
        return [&, window = make_window(target, ordering)](size_t batch_size) {
            window->write_block(0, data.data(), batch_size);
        };
    });
    export_results(bench, context_variables);
}
