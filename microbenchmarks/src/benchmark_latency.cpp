// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "tt-umd-workload/cluster.hpp"
#include "tt-umd/arch/wormhole_implementation.hpp"
#include "tt-umd/firmware/firmware_info_provider.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "utils.hpp"

using namespace tt::umd;
using namespace tt::umd::microbenchmarks;

class Latency : public ClusterBenchmark {};

// Round trip time of the small calls that tools issue one at a time, where the cost of a single call matters and not
// the throughput.
TEST_F(Latency, SingleCall) {
    const CoreCoord tensix_core = cluster->get_soc_descriptor(mmio_chip).get_cores(tt::CoreType::TENSIX).front();
    // An L1 address, so the register write does not change the state of the core.
    const uint64_t address = 0x1000;
    FirmwareInfoProvider* firmware_info = cluster->get_tt_device(mmio_chip)->get_firmware_info_provider();
    uint32_t value = 0;

    const std::vector<std::string> context_variables = {"operation"};
    Bench bench;
    bench.title("Latency").unit("op").performanceCounters(false).warmup(1);

    auto benchmark = [&](const std::string& operation, auto run) {
        bench.context("operation", operation);
        bench.run(fmt::format("operation: {}", operation), run);
    };

    benchmark(
        "reg_read", [&] { cluster->read_from_device_reg(&value, mmio_chip, tensix_core, address, sizeof(value)); });
    benchmark(
        "reg_write", [&] { cluster->write_to_device_reg(&value, sizeof(value), mmio_chip, tensix_core, address); });
    benchmark("telemetry_read", [&] { doNotOptimizeAway(firmware_info->get_asic_temperature()); });
    // The message code is specific to the architecture, and only the Wormhole one is known to work here.
    if (cluster->get_soc_descriptor(mmio_chip).arch == tt::ARCH::WORMHOLE_B0) {
        const uint32_t nop_message =
            wormhole::ARC_MSG_COMMON_PREFIX | static_cast<uint32_t>(wormhole::arc_message_type::NOP);
        benchmark("arc_message", [&] { cluster->arc_msg(mmio_chip, nop_message); });
    }
    export_results(bench, context_variables);
}
