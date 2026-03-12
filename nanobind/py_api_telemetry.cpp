// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include "umd/device/arc/arc_telemetry_reader.hpp"
#include "umd/device/arc/smbus_arc_telemetry_reader.hpp"
#include "umd/device/firmware/firmware_info_provider.hpp"
#include "umd/device/types/gddr_telemetry.hpp"
#include "umd/device/types/telemetry.hpp"
#include "umd/device/types/wormhole_telemetry.hpp"

namespace nb = nanobind;

using namespace tt::umd;

void bind_telemetry(nb::module_& m) {
    // Create a submodule for wormhole, so that we can expose telemetry through it.
    // The submodule matches namespace used in C++.
    auto wormhole = m.def_submodule("wormhole", "Wormhole-related functionality");

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
        .value("DM_BL_FW_VERSION", wormhole::TelemetryTag::DM_BL_FW_VERSION)
        .value("DM_APP_FW_VERSION", wormhole::TelemetryTag::DM_APP_FW_VERSION)
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

    // Universal telemetry tags for all archs for newer firmware.
    nb::enum_<TelemetryTag>(m, "TelemetryTag")
        .value("BOARD_ID_HIGH", TelemetryTag::BOARD_ID_HIGH)
        .value("BOARD_ID_LOW", TelemetryTag::BOARD_ID_LOW)
        .value("ASIC_ID", TelemetryTag::ASIC_ID)
        .value("HARVESTING_STATE", TelemetryTag::HARVESTING_STATE)
        .value("UPDATE_TELEM_SPEED", TelemetryTag::UPDATE_TELEM_SPEED)
        .value("VCORE", TelemetryTag::VCORE)
        .value("TDP", TelemetryTag::TDP)
        .value("TDC", TelemetryTag::TDC)
        .value("VDD_LIMITS", TelemetryTag::VDD_LIMITS)
        .value("THM_LIMIT_SHUTDOWN", TelemetryTag::THM_LIMIT_SHUTDOWN)
        .value("ASIC_TEMPERATURE", TelemetryTag::ASIC_TEMPERATURE)
        .value("VREG_TEMPERATURE", TelemetryTag::VREG_TEMPERATURE)
        .value("BOARD_TEMPERATURE", TelemetryTag::BOARD_TEMPERATURE)
        .value("AICLK", TelemetryTag::AICLK)
        .value("AXICLK", TelemetryTag::AXICLK)
        .value("ARCCLK", TelemetryTag::ARCCLK)
        .value("L2CPUCLK0", TelemetryTag::L2CPUCLK0)
        .value("L2CPUCLK1", TelemetryTag::L2CPUCLK1)
        .value("L2CPUCLK2", TelemetryTag::L2CPUCLK2)
        .value("L2CPUCLK3", TelemetryTag::L2CPUCLK3)
        .value("ETH_LIVE_STATUS", TelemetryTag::ETH_LIVE_STATUS)
        .value("DDR_STATUS", TelemetryTag::GDDR_STATUS)
        .value("GDDR_SPEED", TelemetryTag::GDDR_SPEED)
        .value("ETH_FW_VERSION", TelemetryTag::ETH_FW_VERSION)
        .value("GDDR_FW_VERSION", TelemetryTag::GDDR_FW_VERSION)
        .value("DM_APP_FW_VERSION", TelemetryTag::DM_APP_FW_VERSION)
        .value("DM_BL_FW_VERSION", TelemetryTag::DM_BL_FW_VERSION)
        .value("FLASH_BUNDLE_VERSION", TelemetryTag::FLASH_BUNDLE_VERSION)
        .value("CM_FW_VERSION", TelemetryTag::CM_FW_VERSION)
        .value("L2CPU_FW_VERSION", TelemetryTag::L2CPU_FW_VERSION)
        .value("FAN_SPEED", TelemetryTag::FAN_SPEED)
        .value("TIMER_HEARTBEAT", TelemetryTag::TIMER_HEARTBEAT)
        .value("TELEMETRY_ENUM_COUNT", TelemetryTag::TELEMETRY_ENUM_COUNT)
        .value("ENABLED_TENSIX_COL", TelemetryTag::ENABLED_TENSIX_COL)
        .value("ENABLED_ETH", TelemetryTag::ENABLED_ETH)
        .value("ENABLED_GDDR", TelemetryTag::ENABLED_GDDR)
        .value("ENABLED_L2CPU", TelemetryTag::ENABLED_L2CPU)
        .value("PCIE_USAGE", TelemetryTag::PCIE_USAGE)
        .value("NOC_TRANSLATION", TelemetryTag::NOC_TRANSLATION)
        .value("FAN_RPM", TelemetryTag::FAN_RPM)
        .value("GDDR_0_1_TEMP", TelemetryTag::GDDR_0_1_TEMP)
        .value("GDDR_2_3_TEMP", TelemetryTag::GDDR_2_3_TEMP)
        .value("GDDR_4_5_TEMP", TelemetryTag::GDDR_4_5_TEMP)
        .value("GDDR_6_7_TEMP", TelemetryTag::GDDR_6_7_TEMP)
        .value("GDDR_0_1_CORR_ERRS", TelemetryTag::GDDR_0_1_CORR_ERRS)
        .value("GDDR_2_3_CORR_ERRS", TelemetryTag::GDDR_2_3_CORR_ERRS)
        .value("GDDR_4_5_CORR_ERRS", TelemetryTag::GDDR_4_5_CORR_ERRS)
        .value("GDDR_6_7_CORR_ERRS", TelemetryTag::GDDR_6_7_CORR_ERRS)
        .value("GDDR_UNCORR_ERRS", TelemetryTag::GDDR_UNCORR_ERRS)
        .value("MAX_GDDR_TEMP", TelemetryTag::MAX_GDDR_TEMP)
        .value("ASIC_LOCATION", TelemetryTag::ASIC_LOCATION)
        .value("BOARD_POWER_LIMIT", TelemetryTag::BOARD_POWER_LIMIT)
        .value("TDC_LIMIT_MAX", TelemetryTag::TDC_LIMIT_MAX)
        .value("THM_LIMIT_THROTTLE", TelemetryTag::THM_LIMIT_THROTTLE)
        .value("TT_FLASH_VERSION", TelemetryTag::TT_FLASH_VERSION)
        .value("THERM_TRIP_COUNT", TelemetryTag::THERM_TRIP_COUNT)
        .value("ASIC_ID_HIGH", TelemetryTag::ASIC_ID_HIGH)
        .value("ASIC_ID_LOW", TelemetryTag::ASIC_ID_LOW)
        .value("AICLK_LIMIT_MAX", TelemetryTag::AICLK_LIMIT_MAX)
        .value("TDP_LIMIT_MAX", TelemetryTag::TDP_LIMIT_MAX)
        .value("NUMBER_OF_TAGS", TelemetryTag::NUMBER_OF_TAGS)
        .def("__int__", [](TelemetryTag tag) { return static_cast<int>(tag); });

    nb::enum_<GddrModule>(m, "GddrModule", "GDDR module indices for Blackhole")
        .value("GDDR_0", GddrModule::GDDR_0)
        .value("GDDR_1", GddrModule::GDDR_1)
        .value("GDDR_2", GddrModule::GDDR_2)
        .value("GDDR_3", GddrModule::GDDR_3)
        .value("GDDR_4", GddrModule::GDDR_4)
        .value("GDDR_5", GddrModule::GDDR_5)
        .value("GDDR_6", GddrModule::GDDR_6)
        .value("GDDR_7", GddrModule::GDDR_7)
        .def("__int__", [](GddrModule gddr) { return static_cast<int>(gddr); });

    nb::class_<GddrModuleTelemetry>(m, "GddrModuleTelemetry", "Per-module GDDR telemetry (temp, errors, status).")
        .def_ro("dram_temperature_top", &GddrModuleTelemetry::dram_temperature_top)
        .def_ro("dram_temperature_bottom", &GddrModuleTelemetry::dram_temperature_bottom)
        .def_ro("corr_edc_rd_errors", &GddrModuleTelemetry::corr_edc_rd_errors)
        .def_ro("corr_edc_wr_errors", &GddrModuleTelemetry::corr_edc_wr_errors)
        .def_ro("uncorr_edc_rd_error", &GddrModuleTelemetry::uncorr_edc_rd_error)
        .def_ro("uncorr_edc_wr_error", &GddrModuleTelemetry::uncorr_edc_wr_error);

    nb::class_<GddrTelemetry>(
        m, "GddrTelemetry", "Aggregated GDDR telemetry for monitoring/early warning of DRAM failure.")
        .def_prop_ro("modules", [](const GddrTelemetry& t) {
            nb::dict d;
            for (const auto& [key, value] : t.modules) {
                d[nb::cast(key)] = nb::cast(value);
            }
            return d;
        });

    nb::class_<ArcTelemetryReader>(m, "ArcTelemetryReader")
        .def("read_entry", &ArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &ArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));

    // SmBusArcTelemetryReader binding - for direct instantiation when SMBUS telemetry is needed.
    nb::class_<SmBusArcTelemetryReader, ArcTelemetryReader>(m, "SmBusArcTelemetryReader")
        .def(nb::init<TTDevice*>(), nb::arg("tt_device"))
        .def("read_entry", &SmBusArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &SmBusArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));

    nb::enum_<tt::DramTrainingStatus>(m, "DramTrainingStatus")
        .value("IN_PROGRESS", tt::DramTrainingStatus::IN_PROGRESS)
        .value("FAIL", tt::DramTrainingStatus::FAIL)
        .value("SUCCESS", tt::DramTrainingStatus::SUCCESS)
        .def("__int__", [](tt::DramTrainingStatus status) { return static_cast<int>(status); });

    nb::class_<FirmwareInfoProvider>(m, "FirmwareInfoProvider")
        .def("get_firmware_version", &FirmwareInfoProvider::get_firmware_version)
        .def("get_board_id", &FirmwareInfoProvider::get_board_id)
        .def("get_eth_fw_version", &FirmwareInfoProvider::get_eth_fw_version)
        .def("get_asic_location", &FirmwareInfoProvider::get_asic_location)
        .def("get_aiclk", &FirmwareInfoProvider::get_aiclk)
        .def("get_axiclk", &FirmwareInfoProvider::get_axiclk)
        .def("get_arcclk", &FirmwareInfoProvider::get_arcclk)
        .def("get_fan_speed", &FirmwareInfoProvider::get_fan_speed)
        .def("get_tdp", &FirmwareInfoProvider::get_tdp)
        .def("get_tdc", &FirmwareInfoProvider::get_tdc)
        .def("get_vcore", &FirmwareInfoProvider::get_vcore)
        .def("get_board_temperature", &FirmwareInfoProvider::get_board_temperature)
        .def("get_dram_training_status", &FirmwareInfoProvider::get_dram_training_status, nb::arg("num_dram_channels"))
        .def("get_max_clock_freq", &FirmwareInfoProvider::get_max_clock_freq)
        .def("get_asic_location", &FirmwareInfoProvider::get_asic_location)
        .def("get_heartbeat", &FirmwareInfoProvider::get_heartbeat)
        .def("get_aggregated_dram_telemetry", &FirmwareInfoProvider::get_aggregated_dram_telemetry)
        .def("get_dram_telemetry", &FirmwareInfoProvider::get_dram_telemetry, nb::arg("gddr_module"))
        .def("get_dram_speed", &FirmwareInfoProvider::get_dram_speed)
        .def("get_current_max_dram_temperature", &FirmwareInfoProvider::get_current_max_dram_temperature)
        .def("get_thm_limit_shutdown", &FirmwareInfoProvider::get_thm_limit_shutdown)
        .def("get_board_power_limit", &FirmwareInfoProvider::get_board_power_limit)
        .def("get_thm_limit_throttle", &FirmwareInfoProvider::get_thm_limit_throttle)
        .def("get_therm_trip_count", &FirmwareInfoProvider::get_therm_trip_count)
        .def("get_eth_heartbeat_status", &FirmwareInfoProvider::get_eth_heartbeat_status)
        .def("get_eth_retrain_status", &FirmwareInfoProvider::get_eth_retrain_status)
        .def_static(
            "get_minimum_compatible_firmware_version",
            &FirmwareInfoProvider::get_minimum_compatible_firmware_version,
            nb::arg("arch"))
        .def_static(
            "get_latest_supported_firmware_version",
            &FirmwareInfoProvider::get_latest_supported_firmware_version,
            nb::arg("arch"))
        .def_static(
            "create_firmware_info_provider",
            &FirmwareInfoProvider::create_firmware_info_provider,
            nb::arg("tt_device"));
}
