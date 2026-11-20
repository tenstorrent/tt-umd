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
#include "tt-umd/chip_helpers/sysmem_buffer.hpp"
#include "tt-umd/chip_helpers/sysmem_manager.hpp"
#include "tt-umd/types/cluster_descriptor_types.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "utils.hpp"

using namespace tt::umd;
using namespace tt::umd::microbenchmarks;

class DMA : public ClusterBenchmark {};

// Transfers through PCIe DMA in both modes: "bounce" copies through the driver's staging buffer, "zero_copy" transfers
// straight from or into a pinned host buffer. Small batches show the fixed cost of a transfer, large ones the
// sustained throughput.
TEST_F(DMA, IOSpeed) {
    struct Target {
        CoreCoord core;
        uint64_t address;
        std::vector<size_t> batch_sizes;
    };

    const SocDescriptor& soc_desc = cluster->get_soc_descriptor(mmio_chip);
    // Up to 8 KiB these are the batch sizes of the TLB benchmark, so that the two can be compared size by size.
    const std::vector<size_t> l1_batch_sizes = {
        64,
        128,
        256,
        512,
        1 * ONE_KIB,
        2 * ONE_KIB,
        4 * ONE_KIB,
        8 * ONE_KIB,
        16 * ONE_KIB,
        64 * ONE_KIB,
        256 * ONE_KIB,
        512 * ONE_KIB,
        1 * ONE_MIB};
    std::vector<size_t> dram_batch_sizes = l1_batch_sizes;
    dram_batch_sizes.insert(
        dram_batch_sizes.end(), {2 * ONE_MIB, 4 * ONE_MIB, 8 * ONE_MIB, 16 * ONE_MIB, 32 * ONE_MIB});
    // Tensix L1 has no room for more than 1 MiB.
    const std::vector<Target> targets = {
        {soc_desc.get_cores(tt::CoreType::TENSIX).front(), 0x1000, l1_batch_sizes},
        {soc_desc.get_cores(tt::CoreType::DRAM).front(), 0x0, dram_batch_sizes},
    };
    const size_t max_batch_size = dram_batch_sizes.back();

    std::vector<std::string> directions = {"write"};
    // Blackhole has no device to host DMA.
    if (soc_desc.arch != tt::ARCH::BLACKHOLE) {
        directions.push_back("read");
    }

    std::vector<uint8_t> data(max_batch_size);
    std::unique_ptr<SysmemBuffer> pinned_buffer =
        cluster->get_chip(mmio_chip)->get_sysmem_manager()->allocate_sysmem_buffer(max_batch_size);

    const std::vector<std::string> context_variables = {"mode", "direction", "core", "coretype"};
    Bench bench;
    bench.title("DMA").unit("byte").performanceCounters(false).warmup(1);

    // make_operation(target) returns a callable taking the batch size, built outside the timed region.
    auto benchmark = [&](const std::string& mode, const std::string& direction, auto make_operation) {
        for (const Target& target : targets) {
            auto operation = make_operation(target);
            for (const size_t batch_size : target.batch_sizes) {
                const std::string core = target.core.str();
                const std::string core_type = to_str(target.core.core_type);
                bench.context("mode", mode)
                    .context("direction", direction)
                    .context("core", core)
                    .context("coretype", core_type)
                    .batch(batch_size);
                const std::string name = fmt::format(
                    "mode: {}, direction: {}, core: {}, coretype: {}, batch: {}",
                    mode,
                    direction,
                    core,
                    core_type,
                    format_with_unit(batch_size, "byte"));
                bench.run(name, [&] { operation(batch_size); });
            }
        }
    };

    for (const std::string& direction : directions) {
        const bool is_write = direction == "write";
        benchmark("bounce", direction, [&](const Target& target) {
            return [&, core = target.core, address = target.address](size_t batch_size) {
                if (is_write) {
                    cluster->dma_write_to_device(data.data(), batch_size, mmio_chip, core, address);
                } else {
                    cluster->dma_read_from_device(data.data(), batch_size, mmio_chip, core, address);
                }
            };
        });
        benchmark("zero_copy", direction, [&](const Target& target) {
            // The pinned buffer is addressed by the device, which expects translated coordinates.
            const tt_xy_pair core = soc_desc.translate_chip_coord_to_translated(target.core, get_selected_noc_id());
            return [&, core, address = target.address](size_t batch_size) {
                if (is_write) {
                    pinned_buffer->dma_write_to_device(0, batch_size, core, address);
                } else {
                    pinned_buffer->dma_read_from_device(0, batch_size, core, address);
                }
            };
        });
    }
    export_results(bench, context_variables);
}
