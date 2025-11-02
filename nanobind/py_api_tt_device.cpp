/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include <tt-logger/tt-logger.hpp>

#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/remote_wormhole_tt_device.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

// Helper function for easy creation of RemoteWormholeTTDevice
std::unique_ptr<TTDevice> create_remote_wormhole_tt_device(
    TTDevice *local_chip, ClusterDescriptor *cluster_descriptor, ChipId remote_chip_id) {
    // Note: this chip id has to match the local_chip passed. Figure out if there's a better way to do this.
    ChipId local_chip_id = cluster_descriptor->get_closest_mmio_capable_chip(remote_chip_id);
    EthCoord target_chip = cluster_descriptor->get_chip_locations().at(remote_chip_id);
    SocDescriptor local_soc_descriptor = SocDescriptor(local_chip->get_arch(), local_chip->get_chip_info());
    auto remote_communication = RemoteCommunication::create_remote_communication(local_chip, target_chip);
    remote_communication->set_remote_transfer_ethernet_cores(
        local_soc_descriptor.get_eth_xy_pairs_for_channels(cluster_descriptor->get_active_eth_channels(local_chip_id)));
    return TTDevice::create(std::move(remote_communication));
}

void bind_tt_device(nb::module_ &m) {
    nb::enum_<IODeviceType>(m, "IODeviceType").value("PCIe", IODeviceType::PCIe).value("JTAG", IODeviceType::JTAG);

    nb::class_<PciDeviceInfo>(m, "PciDeviceInfo")
        .def_ro("pci_domain", &PciDeviceInfo::pci_domain)
        .def_ro("pci_bus", &PciDeviceInfo::pci_bus)
        .def_ro("pci_device", &PciDeviceInfo::pci_device)
        .def_ro("pci_function", &PciDeviceInfo::pci_function)
        .def_ro("pci_bdf", &PciDeviceInfo::pci_bdf)
        .def("get_arch", &PciDeviceInfo::get_arch);

    nb::class_<PCIDevice>(m, "PCIDevice")
        .def(nb::init<int>())
        .def_static(
            "enumerate_devices",
            [](std::unordered_set<int> pci_target_devices = {}) {
                return PCIDevice::enumerate_devices(pci_target_devices);
            },
            nb::arg("pci_target_devices") = std::unordered_set<int>{},
            "Enumerates PCI devices, optionally filtering by target devices.")
        .def_static(
            "enumerate_devices_info",
            [](std::unordered_set<int> pci_target_devices = {}) {
                return PCIDevice::enumerate_devices_info(pci_target_devices);
            },
            nb::arg("pci_target_devices") = std::unordered_set<int>{},
            "Enumerates PCI device information, optionally filtering by target devices.")
        .def("get_device_info", &PCIDevice::get_device_info);

    nb::class_<TTDevice>(m, "TTDevice")
        .def_static(
            "create",
            static_cast<std::unique_ptr<TTDevice> (*)(int, IODeviceType)>(&TTDevice::create),
            nb::arg("device_number"),
            nb::arg("device_type") = IODeviceType::PCIe,
            nb::rv_policy::take_ownership)
        .def("init_tt_device", &TTDevice::init_tt_device)
        .def("get_arc_telemetry_reader", &TTDevice::get_arc_telemetry_reader, nb::rv_policy::reference_internal)
        .def("get_arch", &TTDevice::get_arch)
        .def("get_board_id", &TTDevice::get_board_id)
        .def("get_board_type", &TTDevice::get_board_type)
        .def("get_pci_device", &TTDevice::get_pci_device, nb::rv_policy::reference)
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
            nb::arg("addr"))
        .def(
            "spi_read",
            [](TTDevice &self, uint32_t addr, nb::bytearray data) -> void {
                uint8_t *data_ptr = reinterpret_cast<uint8_t *>(data.data());
                size_t data_size = data.size();
                self.spi_read(addr, data_ptr, data_size);
            },
            nb::arg("addr"),
            nb::arg("data"),
            "Read data from SPI flash memory")
        .def(
            "spi_write",
            [](TTDevice &self, uint32_t addr, nb::bytes data, bool skip_write_to_spi = false) -> void {
                const char *data_ptr = data.c_str();
                size_t data_size = data.size();
                self.spi_write(addr, reinterpret_cast<const uint8_t *>(data_ptr), data_size, skip_write_to_spi);
            },
            nb::arg("addr"),
            nb::arg("data"),
            nb::arg("skip_write_to_spi") = false,
            "Write data to SPI flash memory. If skip_write_to_spi is True, only writes to buffer without committing to "
            "SPI.")
        .def(
            "arc_msg",
            [](TTDevice &self,
               uint32_t msg_code,
               bool wait_for_done = true,
               std::vector<uint32_t> args = {},
               uint32_t timeout_ms = 1000) -> nb::tuple {
                // Warn if wait_for_done is False
                if (!wait_for_done) {
                    log_warning(
                        tt::LogUMD, "arc_msg: wait_for_done=False is not respected. Message will wait for completion.");
                }
                // For Wormhole, prepend 0xaa00 to the msg_code
                if (self.get_arch() == tt::ARCH::WORMHOLE_B0) {
                    msg_code = wormhole::ARC_MSG_COMMON_PREFIX | msg_code;
                }
                std::vector<uint32_t> return_values = {0, 0};
                uint32_t exit_code = self.get_arc_messenger()->send_message(
                    msg_code, return_values, args, std::chrono::milliseconds(timeout_ms));
                log_info(
                    tt::LogUMD,
                    "arc_msg msg_code={:x}, exit_code={}, return_values={}, return_values[1]={}",
                    msg_code,
                    exit_code,
                    return_values[0],
                    return_values[1]);
                return nb::make_tuple(exit_code, return_values[0], return_values[1]);
            },
            nb::arg("msg_code"),
            nb::arg("wait_for_done") = true,
            nb::arg("args") = std::vector<uint32_t>{},
            nb::arg("timeout_ms") = 1000,
            "Send ARC message and return (exit_code, return_3, return_4). "
            "Args is a list of uint32_t arguments. For Wormhole, max 2 args (each <= 0xFFFF). For Blackhole, max 7 "
            "args.")
        .def(
            "arc_msg",
            [](TTDevice &self,
               uint32_t msg_code,
               bool wait_for_done,
               uint32_t arg0,
               uint32_t arg1,
               uint32_t timeout_ms = 1000) -> nb::tuple {
                // Warn if wait_for_done is False
                if (!wait_for_done) {
                    log_warning(
                        tt::LogUMD, "arc_msg: wait_for_done=False is not respected. Message will wait for completion.");
                }
                // For Wormhole, prepend 0xaa00 to the msg_code
                if (self.get_arch() == tt::ARCH::WORMHOLE_B0) {
                    msg_code = wormhole::ARC_MSG_COMMON_PREFIX | msg_code;
                }
                std::vector<uint32_t> args = {arg0, arg1};
                std::vector<uint32_t> return_values = {0, 0};
                uint32_t exit_code = self.get_arc_messenger()->send_message(
                    msg_code, return_values, args, std::chrono::milliseconds(timeout_ms));
                return nb::make_tuple(exit_code, return_values[0], return_values[1]);
            },
            nb::arg("msg_code"),
            nb::arg("wait_for_done"),
            nb::arg("arg0"),
            nb::arg("arg1"),
            nb::arg("timeout_ms") = 1000,
            "Send ARC message with two arguments and return (exit_code, return_3, return_4).")
        .def(
            "get_spi_fw_bundle_version",
            &TTDevice::get_spi_fw_bundle_version,
            "Get firmware bundle version from SPI (Blackhole only). "
            "Returns semver_t with major.minor.patch components.");

    nb::class_<RemoteWormholeTTDevice, TTDevice>(m, "RemoteWormholeTTDevice");

    m.def(
        "create_remote_wormhole_tt_device",
        &create_remote_wormhole_tt_device,
        nb::arg("local_chip"),
        nb::arg("cluster_descriptor"),
        nb::arg("remote_chip_id"),
        nb::rv_policy::take_ownership,
        "Creates a RemoteWormholeTTDevice for communication with a remote chip.");
}
