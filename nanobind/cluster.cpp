/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/cluster.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include "umd/device/types/wormhole_telemetry.h"

namespace nb = nanobind;

using namespace tt::umd;

NB_MODULE(tt_umd, m) {
    // Expose the Cluster class
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>())
        .def("get_target_device_ids", &Cluster::get_target_device_ids)
        .def("get_clocks", &Cluster::get_clocks);

    // Expose the ArcTelemetryReader class
    nb::class_<ArcTelemetryReader>(m, "ArcTelemetryReader")
        .def("read_entry", &ArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &ArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));

    // Expose the TTDevice class
    nb::class_<TTDevice>(m, "TTDevice")
        .def_static("create", &TTDevice::create, nb::arg("pci_device_number"), nb::rv_policy::take_ownership)
        .def("get_arc_telemetry_reader", &TTDevice::get_arc_telemetry_reader, nb::rv_policy::reference_internal);

    // Expose the PCIDevice class
    nb::class_<PCIDevice>(m, "PCIDevice")
        .def(nb::init<int>())
        // std::vector<int> PCIDevice::enumerate_devices() {
        .def("enumerate_devices", &PCIDevice::enumerate_devices)
        .def("enumerate_devices_info", &PCIDevice::enumerate_devices_info);

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
}
