/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "umd/device/cluster.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/types/blackhole_telemetry.h"
#include "umd/device/types/wormhole_telemetry.h"

namespace nb = nanobind;

using namespace tt::umd;

// Helper function for easy creation of RemoteWormholeTTDevice
std::unique_ptr<RemoteWormholeTTDevice> create_remote_wormhole_tt_device(
    TTDevice *local_chip, tt_ClusterDescriptor *cluster_descriptor, chip_id_t remote_chip_id) {
    eth_coord_t target_chip = cluster_descriptor->get_chip_locations().at(remote_chip_id);
    tt_SocDescriptor local_soc_descriptor = tt_SocDescriptor(
        local_chip->get_arch(),
        local_chip->get_chip_info().noc_translation_enabled,
        local_chip->get_chip_info().harvesting_masks,
        local_chip->get_chip_info().board_type);
    auto remote_communication = std::make_unique<RemoteCommunication>(local_chip);
    remote_communication->set_remote_transfer_ethernet_cores(
        local_soc_descriptor.get_eth_cores_for_channels(cluster_descriptor->get_active_eth_channels(remote_chip_id)));

    return std::make_unique<RemoteWormholeTTDevice>(local_chip, std::move(remote_communication), target_chip);
}

NB_MODULE(tt_umd, m) {
    // Expose the eth_coord_t struct
    nb::class_<eth_coord_t>(m, "EthCoord")
        .def_ro("cluster_id", &eth_coord_t::cluster_id)
        .def_ro("x", &eth_coord_t::x)
        .def_ro("y", &eth_coord_t::y)
        .def_ro("rack", &eth_coord_t::rack)
        .def_ro("shelf", &eth_coord_t::shelf);

    // Expose the tt_xy_pair
    nb::class_<tt::umd::xy_pair>(m, "tt_xy_pair")
        .def(nb::init<uint32_t, uint32_t>(), nb::arg("x"), nb::arg("y"))
        .def_ro("x", &tt_xy_pair::x)
        .def_ro("y", &tt_xy_pair::y)
        .def("__str__", [](const tt_xy_pair &pair) { return fmt::format("({}, {})", pair.x, pair.y); });

    // Expose PciDeviceInfo
    nb::class_<PciDeviceInfo>(m, "PciDeviceInfo")
        .def_ro("pci_domain", &PciDeviceInfo::pci_domain)
        .def_ro("pci_bus", &PciDeviceInfo::pci_bus)
        .def_ro("pci_device", &PciDeviceInfo::pci_device)
        .def_ro("pci_function", &PciDeviceInfo::pci_function)
        .def("get_pci_bdf", &PciDeviceInfo::get_pci_bdf)
        .def("get_arch", &PciDeviceInfo::get_arch);

    // static std::vector<int> enumerate_devices(std::unordered_set<int> pci_target_devices = {});
    // Expose the PCIDevice class
    nb::class_<PCIDevice>(m, "PCIDevice")
        .def(nb::init<int>())
        // std::vector<int> PCIDevice::enumerate_devices() {
        .def_static(
            "enumerate_devices",
            [](std::unordered_set<int> pci_target_devices = {}) {
                return PCIDevice::enumerate_devices(pci_target_devices);
            },
            nb::arg("pci_target_devices") = std::unordered_set<int>{},
            "Enumerates PCI devices, optionally filtering by target devices.")
        .def_static("enumerate_devices_info", &PCIDevice::enumerate_devices_info)
        .def("get_device_info", &PCIDevice::get_device_info);

    // Expose tt::ARCH enum
    nb::enum_<tt::ARCH>(m, "ARCH")
        .value("GRAYSKULL", tt::ARCH::GRAYSKULL)
        .value("WORMHOLE_B0", tt::ARCH::WORMHOLE_B0)
        .value("BLACKHOLE", tt::ARCH::BLACKHOLE)
        .value("QUASAR", tt::ARCH::QUASAR)
        .value("Invalid", tt::ARCH::Invalid)
        .def("__str__", &tt::arch_to_str)
        .def("__int__", [](tt::ARCH tag) { return static_cast<int>(tag); })
        .def_static("from_str", &tt::arch_from_str, nb::arg("arch_str"));

    // Expose the ClusterDescriptor class
    nb::class_<tt_ClusterDescriptor>(m, "ClusterDescriptor")
        .def("get_all_chips", &tt_ClusterDescriptor::get_all_chips)
        .def("is_chip_mmio_capable", &tt_ClusterDescriptor::is_chip_mmio_capable, nb::arg("chip_id"))
        .def("is_chip_remote", &tt_ClusterDescriptor::is_chip_remote, nb::arg("chip_id"))
        .def("get_closest_mmio_capable_chip", &tt_ClusterDescriptor::get_closest_mmio_capable_chip, nb::arg("chip"))
        .def("get_chips_local_first", &tt_ClusterDescriptor::get_chips_local_first, nb::arg("chips"))
        .def("get_chip_locations", &tt_ClusterDescriptor::get_chip_locations)
        .def("get_chips_with_mmio", &tt_ClusterDescriptor::get_chips_with_mmio)
        .def("get_active_eth_channels", &tt_ClusterDescriptor::get_active_eth_channels, nb::arg("chip_id"))
        .def("get_arch", &tt_ClusterDescriptor::get_arch, nb::arg("chip_id"));

    // Expose the Cluster class
    nb::class_<Cluster>(m, "Cluster")
        .def(nb::init<>())
        .def("get_target_device_ids", &Cluster::get_target_device_ids)
        .def("get_clocks", &Cluster::get_clocks)
        .def_static(
            "create_cluster_descriptor",
            [](std::string sdesc_path, std::unordered_set<chip_id_t> pci_target_devices) {
                return Cluster::create_cluster_descriptor(std::move(sdesc_path), std::move(pci_target_devices));
            },
            nb::arg("sdesc_path"),
            nb::arg("pci_target_devices"),
            nb::rv_policy::take_ownership);

    // Expose the ArcTelemetryReader class
    nb::class_<ArcTelemetryReader>(m, "ArcTelemetryReader")
        .def("read_entry", &ArcTelemetryReader::read_entry, nb::arg("telemetry_tag"))
        .def("is_entry_available", &ArcTelemetryReader::is_entry_available, nb::arg("telemetry_tag"));

    // Expose the TTDevice class
    nb::class_<TTDevice>(m, "TTDevice")
        .def_static("create", &TTDevice::create, nb::arg("pci_device_number"), nb::rv_policy::take_ownership)
        .def("get_arc_telemetry_reader", &TTDevice::get_arc_telemetry_reader, nb::rv_policy::reference_internal)
        .def("get_arch", &TTDevice::get_arch)
        .def("get_board_id", &TTDevice::get_board_id)
        .def("get_pci_device", &TTDevice::get_pci_device, nb::rv_policy::reference)
        // .def("read_from_device",
        //     [](TTDevice &self, nb::capsule mem, tt_xy_pair core, uint64_t addr, uint32_t size) {
        //         self.read_from_device((void*)mem.data(), core, addr, size);
        //     },
        //     nb::arg("mem_ptr"),
        //     nb::arg("core"),
        //     nb::arg("addr"),
        //     nb::arg("size"),
        //     "Reads data from the device into the provided memory buffer."
        // )
        .def(
            "noc_read32",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr) -> uint32_t {
                tt_xy_pair core = {core_x, core_y};
                uint32_t value = 0;
                self.read_from_device(&value, core, addr, sizeof(uint32_t));
                return value;
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"));

    // // Expose the LocalChip class
    // nb::class_<LocalChip>(m, "LocalChip")
    //     .def(nb::init<std::unique_ptr<TTDevice>>(), nb::arg("tt_device"))
    //     .def("get_tt_device", &LocalChip::get_tt_device, nb::rv_policy::reference_internal)
    //     .def(
    //         "set_remote_transfer_ethernet_cores",
    //         static_cast<void (LocalChip::*)(const std::set<uint32_t> &)>(
    //             &LocalChip::set_remote_transfer_ethernet_cores),
    //         nb::arg("channels"));

    // Expose RemoteCommunication class
    // void set_remote_transfer_ethernet_cores(const std::vector<tt_xy_pair>& cores);
    // nb::class_<RemoteCommunication>(m, "RemoteCommunication")
    //     .def(nb::init<TTDevice *>(), nb::arg("local_chip"))
    //     .def("set_remote_transfer_ethernet_cores",
    //          &RemoteCommunication::set_remote_transfer_ethernet_cores,
    //          nb::arg("cores"),
    //          "Sets the remote transfer ethernet cores for communication.");

    // Expose the RemoteWormholeTTDevice class
    nb::class_<RemoteWormholeTTDevice, TTDevice>(m, "RemoteWormholeTTDevice");

    // Expose create_remote_wormhole_tt_device
    m.def(
        "create_remote_wormhole_tt_device",
        &create_remote_wormhole_tt_device,
        nb::arg("local_chip"),
        nb::arg("cluster_descriptor"),
        nb::arg("remote_chip_id"),
        nb::rv_policy::take_ownership,
        "Creates a RemoteWormholeTTDevice for communication with a remote chip.");

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
}
