// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <nanobench.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/microbenchmark_utils.hpp"
#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/telemetry.hpp"

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::test::utils;

// All defined TelemetryTag values (excluding NUMBER_OF_TAGS which is a sentinel).
static constexpr std::array<TelemetryTag, 47> ALL_TELEMETRY_TAGS = {{
    TelemetryTag::BOARD_ID_HIGH,
    TelemetryTag::BOARD_ID_LOW,
    TelemetryTag::ASIC_ID,
    TelemetryTag::HARVESTING_STATE,
    TelemetryTag::UPDATE_TELEM_SPEED,
    TelemetryTag::VCORE,
    TelemetryTag::TDP,
    TelemetryTag::TDC,
    TelemetryTag::VDD_LIMITS,
    TelemetryTag::THM_LIMITS,
    TelemetryTag::ASIC_TEMPERATURE,
    TelemetryTag::VREG_TEMPERATURE,
    TelemetryTag::BOARD_TEMPERATURE,
    TelemetryTag::AICLK,
    TelemetryTag::AXICLK,
    TelemetryTag::ARCCLK,
    TelemetryTag::L2CPUCLK0,
    TelemetryTag::L2CPUCLK1,
    TelemetryTag::L2CPUCLK2,
    TelemetryTag::L2CPUCLK3,
    TelemetryTag::ETH_LIVE_STATUS,
    TelemetryTag::DDR_STATUS,
    TelemetryTag::DDR_SPEED,
    TelemetryTag::ETH_FW_VERSION,
    TelemetryTag::GDDR_FW_VERSION,
    TelemetryTag::DM_APP_FW_VERSION,
    TelemetryTag::DM_BL_FW_VERSION,
    TelemetryTag::FLASH_BUNDLE_VERSION,
    TelemetryTag::CM_FW_VERSION,
    TelemetryTag::L2CPU_FW_VERSION,
    TelemetryTag::FAN_SPEED,
    TelemetryTag::TIMER_HEARTBEAT,
    TelemetryTag::TELEMETRY_ENUM_COUNT,
    TelemetryTag::ENABLED_TENSIX_COL,
    TelemetryTag::ENABLED_ETH,
    TelemetryTag::ENABLED_GDDR,
    TelemetryTag::ENABLED_L2CPU,
    TelemetryTag::PCIE_USAGE,
    TelemetryTag::NOC_TRANSLATION,
    TelemetryTag::FAN_RPM,
    TelemetryTag::ASIC_LOCATION,
    TelemetryTag::TDC_LIMIT_MAX,
    TelemetryTag::TT_FLASH_VERSION,
    TelemetryTag::ASIC_ID_HIGH,
    TelemetryTag::ASIC_ID_LOW,
    TelemetryTag::AICLK_LIMIT_MAX,
    TelemetryTag::TDP_LIMIT_MAX,
}};

TEST(MicrobenchmarkTelemetry, BulkVsIndividualRead) {
    auto bench = ankerl::nanobench::Bench().title("TelemetryRead").unit("read");

    for (int id : PCIDevice::enumerate_devices()) {
        auto tt_device = TTDevice::create(id);
        tt_device->init_tt_device();
        ArcTelemetryReader* telemetry = tt_device->get_arc_telemetry_reader();

        // Filter to only tags available on this device.
        std::vector<TelemetryTag> available_tags;
        for (TelemetryTag tag : ALL_TELEMETRY_TAGS) {
            if (telemetry->is_entry_available(static_cast<uint8_t>(tag))) {
                available_tags.push_back(tag);
            }
        }
        if (available_tags.empty()) {
            continue;
        }

        bench.batch(available_tags.size())
            .name(fmt::format("dev{}_individual_{}_tags", id, available_tags.size()))
            .run([&]() {
                for (TelemetryTag tag : available_tags) {
                    ankerl::nanobench::doNotOptimizeAway(telemetry->read_entry(static_cast<uint8_t>(tag)));
                }
            });

        bench.batch(available_tags.size())
            .name(fmt::format("dev{}_bulk_{}_tags", id, available_tags.size()))
            .run([&]() { ankerl::nanobench::doNotOptimizeAway(telemetry->read_all_entries()); });
    }

    export_results(bench);
}
