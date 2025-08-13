/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>

#include "umd/device/arc/arc_telemetry_reader.h"
#include "umd/device/types/blackhole_telemetry.h"
#include "umd/device/types/wormhole_telemetry.h"

namespace nb = nanobind;

using namespace tt::umd;

void bind_telemetry(nb::module_ &m) {
    // Create a submodule for wormhole
    auto wormhole = m.def_submodule("wormhole", "Wormhole-related functionality");

    // Expose the TelemetryTag enum in the wormhole submodule
    nb::enum_<wormhole::TelemetryTag>(wormhole, "TelemetryTag")
        .value("ENUM_VERSION", wormhole::TelemetryTag::ENUM_VERSION)
        .value("DEVICE_ID", wormhole::TelemetryTag::DEVICE_ID)
        .value("ASIC_RO", wormhole::TelemetryTag::ASIC_RO)
        .value("ASIC_IDD", wormhole::TelemetryTag::ASIC_IDD)
        .value("BOARD_ID_HIGH", wormhole::TelemetryTag::BOARD_ID_HIGH)
        .value("BOARD_ID_LOW", wormhole::TelemetryTag::BOARD_ID_LOW)
        .value("ARC0_FW_VERSION", wormhole::TelemetryTag::ARC0_FW_VERSION)
        .value("ARC1_FW_VERSION", wormhole::TelemetryTag::ARC1_FW_VERSION)
        .value("ARC2_FW_VERSION", wormhole::TelemetryTag::ARC2_FW_VERSION)
        .value("ARC3_FW_VERSION", wormhole::TelemetryTag::ARC3_FW_VERSION)
        .value("SPIBOOTROM_FW_VERSION", wormhole::TelemetryTag::SPIBOOTROM_FW_VERSION)
        .value("ETH_FW_VERSION", wormhole::TelemetryTag::ETH_FW_VERSION)
        .value("M3_BL_FW_VERSION", wormhole::TelemetryTag::M3_BL_FW_VERSION)
        .value("M3_APP_FW_VERSION", wormhole::TelemetryTag::M3_APP_FW_VERSION)
        .value("DDR_STATUS", wormhole::TelemetryTag::DDR_STATUS)
        .value("ETH_STATUS0", wormhole::TelemetryTag::ETH_STATUS0)
        .value("ETH_STATUS1", wormhole::TelemetryTag::ETH_STATUS1)
        .value("PCIE_STATUS", wormhole::TelemetryTag::PCIE_STATUS)
        .value("FAULTS", wormhole::TelemetryTag::FAULTS)
        .value("ARC0_HEALTH", wormhole::TelemetryTag::ARC0_HEALTH)
        .value("ARC1_HEALTH", wormhole::TelemetryTag::ARC1_HEALTH)
        .value("ARC2_HEALTH", wormhole::TelemetryTag::ARC2_HEALTH)
        .value("ARC3_HEALTH", wormhole::TelemetryTag::ARC3_HEALTH)
        .value("FAN_SPEED", wormhole::TelemetryTag::FAN_SPEED)
        .value("AICLK", wormhole::TelemetryTag::AICLK)
        .value("AXICLK", wormhole::TelemetryTag::AXICLK)
        .value("ARCCLK", wormhole::TelemetryTag::ARCCLK)
        .value("THROTTLER", wormhole::TelemetryTag::THROTTLER)
        .value("VCORE", wormhole::TelemetryTag::VCORE)
        .value("ASIC_TEMPERATURE", wormhole::TelemetryTag::ASIC_TEMPERATURE)
        .value("VREG_TEMPERATURE", wormhole::TelemetryTag::VREG_TEMPERATURE)
        .value("BOARD_TEMPERATURE", wormhole::TelemetryTag::BOARD_TEMPERATURE)
        .value("TDP", wormhole::TelemetryTag::TDP)
        .value("TDC", wormhole::TelemetryTag::TDC)
        .value("VDD_LIMITS", wormhole::TelemetryTag::VDD_LIMITS)
        .value("THM_LIMITS", wormhole::TelemetryTag::THM_LIMITS)
        .value("WH_FW_DATE", wormhole::TelemetryTag::WH_FW_DATE)
        .value("ASIC_TMON0", wormhole::TelemetryTag::ASIC_TMON0)
        .value("ASIC_TMON1", wormhole::TelemetryTag::ASIC_TMON1)
        .value("MVDDQ_POWER", wormhole::TelemetryTag::MVDDQ_POWER)
        .value("GDDR_TRAIN_TEMP0", wormhole::TelemetryTag::GDDR_TRAIN_TEMP0)
        .value("GDDR_TRAIN_TEMP1", wormhole::TelemetryTag::GDDR_TRAIN_TEMP1)
        .value("BOOT_DATE", wormhole::TelemetryTag::BOOT_DATE)
        .value("RT_SECONDS", wormhole::TelemetryTag::RT_SECONDS)
        .value("ETH_DEBUG_STATUS0", wormhole::TelemetryTag::ETH_DEBUG_STATUS0)
        .value("ETH_DEBUG_STATUS1", wormhole::TelemetryTag::ETH_DEBUG_STATUS1)
        .value("TT_FLASH_VERSION", wormhole::TelemetryTag::TT_FLASH_VERSION)
        .value("ETH_LOOPBACK_STATUS", wormhole::TelemetryTag::ETH_LOOPBACK_STATUS)
        .value("ETH_LIVE_STATUS", wormhole::TelemetryTag::ETH_LIVE_STATUS)
        .value("FW_BUNDLE_VERSION", wormhole::TelemetryTag::FW_BUNDLE_VERSION)
        .value("NUMBER_OF_TAGS", wormhole::TelemetryTag::NUMBER_OF_TAGS)
        .def("__int__", [](wormhole::TelemetryTag tag) { return static_cast<int>(tag); });

    // Create a submodule for wormhole
    auto blackhole = m.def_submodule("blackhole", "Blackhole-related functionality");

    // Expose the TelemetryTag enum in the blackhole submodule
    nb::enum_<blackhole::TelemetryTag>(blackhole, "TelemetryTag")  // Use 'm' or 'blackhole' submodule
        .value("BOARD_ID_HIGH", blackhole::TelemetryTag::BOARD_ID_HIGH)
        .value("BOARD_ID_LOW", blackhole::TelemetryTag::BOARD_ID_LOW)
        .value("ASIC_ID", blackhole::TelemetryTag::ASIC_ID)
        .value("HARVESTING_STATE", blackhole::TelemetryTag::HARVESTING_STATE)
        .value("UPDATE_TELEM_SPEED", blackhole::TelemetryTag::UPDATE_TELEM_SPEED)
        .value("VCORE", blackhole::TelemetryTag::VCORE)
        .value("TDP", blackhole::TelemetryTag::TDP)
        .value("TDC", blackhole::TelemetryTag::TDC)
        .value("VDD_LIMITS", blackhole::TelemetryTag::VDD_LIMITS)
        .value("THM_LIMITS", blackhole::TelemetryTag::THM_LIMITS)
        .value("ASIC_TEMPERATURE", blackhole::TelemetryTag::ASIC_TEMPERATURE)
        .value("VREG_TEMPERATURE", blackhole::TelemetryTag::VREG_TEMPERATURE)
        .value("BOARD_TEMPERATURE", blackhole::TelemetryTag::BOARD_TEMPERATURE)
        .value("AICLK", blackhole::TelemetryTag::AICLK)
        .value("AXICLK", blackhole::TelemetryTag::AXICLK)
        .value("ARCCLK", blackhole::TelemetryTag::ARCCLK)
        .value("L2CPUCLK0", blackhole::TelemetryTag::L2CPUCLK0)
        .value("L2CPUCLK1", blackhole::TelemetryTag::L2CPUCLK1)
        .value("L2CPUCLK2", blackhole::TelemetryTag::L2CPUCLK2)
        .value("L2CPUCLK3", blackhole::TelemetryTag::L2CPUCLK3)
        .value("ETH_LIVE_STATUS", blackhole::TelemetryTag::ETH_LIVE_STATUS)
        .value("DDR_STATUS", blackhole::TelemetryTag::DDR_STATUS)
        .value("DDR_SPEED", blackhole::TelemetryTag::DDR_SPEED)
        .value("ETH_FW_VERSION", blackhole::TelemetryTag::ETH_FW_VERSION)
        .value("DDR_FW_VERSION", blackhole::TelemetryTag::DDR_FW_VERSION)
        .value("BM_APP_FW_VERSION", blackhole::TelemetryTag::BM_APP_FW_VERSION)
        .value("BM_BL_FW_VERSION", blackhole::TelemetryTag::BM_BL_FW_VERSION)
        .value("FLASH_BUNDLE_VERSION", blackhole::TelemetryTag::FLASH_BUNDLE_VERSION)
        .value("CM_FW_VERSION", blackhole::TelemetryTag::CM_FW_VERSION)
        .value("L2CPU_FW_VERSION", blackhole::TelemetryTag::L2CPU_FW_VERSION)
        .value("FAN_SPEED", blackhole::TelemetryTag::FAN_SPEED)
        .value("TIMER_HEARTBEAT", blackhole::TelemetryTag::TIMER_HEARTBEAT)
        .value("TELEMETRY_ENUM_COUNT", blackhole::TelemetryTag::TELEMETRY_ENUM_COUNT)
        .value("ENABLED_TENSIX_COL", blackhole::TelemetryTag::ENABLED_TENSIX_COL)
        .value("ENABLED_ETH", blackhole::TelemetryTag::ENABLED_ETH)
        .value("ENABLED_GDDR", blackhole::TelemetryTag::ENABLED_GDDR)
        .value("ENABLED_L2CPU", blackhole::TelemetryTag::ENABLED_L2CPU)
        .value("PCIE_USAGE", blackhole::TelemetryTag::PCIE_USAGE)
        .value("NUMBER_OF_TAGS", blackhole::TelemetryTag::NUMBER_OF_TAGS)
        .def("__int__", [](blackhole::TelemetryTag tag) { return static_cast<int>(tag); });

    // Expose the ArcTelemetryReader class
    nb::class_<ArcTelemetryReader>(m, "ArcTelemetryReader")
        .def("read_entry", &ArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &ArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));
}
