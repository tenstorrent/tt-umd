// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include <tt-logger/tt-logger.hpp>

#include "umd/device/arc/spi_tt_device.hpp"
#include "umd/device/arch/wormhole_implementation.hpp"
#include "umd/device/cluster.hpp"
#include "umd/device/pcie/pci_device.hpp"
#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/remote_communication.hpp"
#include "umd/device/tt_device/rtl_simulation_tt_device.hpp"
#include "umd/device/tt_device/simulation_device_factory.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/tt_device/tt_sim_tt_device.hpp"
#include "umd/device/types/communication_protocol.hpp"
#include "umd/device/types/core_coordinates.hpp"
#include "umd/device/types/risc_type.hpp"
#include "umd/device/utils/error.hpp"
namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads. Inside lambdas that need to keep the GIL for argument
// marshalling, use a scoped nb::gil_scoped_release release; block around the native
// call instead.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

// RAII wrapper around Py_buffer so the device read/write bindings can accept
// anything that supports the buffer protocol — bytes, bytearray, memoryview —
// instead of forcing callers to materialize an extra copy first.
//
// The `writable` constructor flag selects the access mode:
//   - writable == false (default): requests PyBUF_SIMPLE. Use for transfers that
//     only READ from the buffer (device writes); read-only exporters such as
//     bytes are accepted. Read the data through readable_data() (const void*).
//   - writable == true: requests PyBUF_WRITABLE. Use for transfers that WRITE
//     into the buffer (device reads); read-only exporters (bytes, a read-only
//     memoryview) are rejected with a BufferError instead of silently discarding
//     the result. Fill the buffer through writable_data() (void*).
//
// Both modes request only a C-contiguous buffer, so non-contiguous exporters
// (e.g. a strided memoryview such as memoryview(b"...")[::2], or a non-contiguous
// NumPy view) are rejected with a BufferError instead of silently producing
// wrong data; callers must pass a contiguous buffer.
//
// Acquire/release must happen with the GIL held; callers are expected to keep
// this object alive across any nb::gil_scoped_release block that uses
// readable_data()/writable_data(). Since the device transfer runs with the GIL released,
// the caller owns the buffer for the duration of the call and must not mutate it
// concurrently from another thread.
class PyBufferView {
public:
    explicit PyBufferView(nb::handle obj, bool writable = false) : writable_(writable) {
        if (PyObject_GetBuffer(obj.ptr(), &buffer_, writable ? PyBUF_WRITABLE : PyBUF_SIMPLE) != 0) {
            throw nb::python_error();
        }
    }

    ~PyBufferView() { PyBuffer_Release(&buffer_); }

    PyBufferView(const PyBufferView &) = delete;
    PyBufferView &operator=(const PyBufferView &) = delete;

    // Read-only view of the data, for transfers that read FROM the buffer.
    const void *readable_data() const { return buffer_.buf; }

    // Writable view of the data, for transfers that write INTO the buffer. Only
    // valid on a view constructed with writable == true.
    void *writable_data() const {
        if (!writable_) {
            UMD_THROW(tt::umd::error::RuntimeError, "PyBufferView::writable_data() called on a read-only view");
        }
        return buffer_.buf;
    }

    size_t size() const { return static_cast<size_t>(buffer_.len); }

private:
    Py_buffer buffer_{};
    bool writable_ = false;
};

using namespace tt;
using namespace tt::umd;

// Helper function for easy creation of a remote TTDevice.
std::unique_ptr<TTDevice> create_remote_tt_device(
    TTDevice *local_tt_device, ClusterDescriptor *cluster_descriptor, ChipId remote_chip_id) {
    // Note: this chip id has to match the local_chip passed. Figure out if there's a better way to do this.
    ChipId local_chip_id = cluster_descriptor->get_closest_mmio_capable_chip(remote_chip_id);
    EthCoord target_chip = cluster_descriptor->get_chip_locations().at(remote_chip_id);
    auto remote_communication = RemoteCommunication::create_remote_communication(local_tt_device, target_chip);
    if (!remote_communication) {
        UMD_THROW(
            error::RuntimeError,
            std::string("Remote communication is not supported for ") + arch_to_str(local_tt_device->get_arch()) +
                " architecture.");
    }
    remote_communication->set_remote_transfer_ethernet_cores(
        local_tt_device->get_soc_descriptor().get_eth_xy_pairs_for_channels(
            cluster_descriptor->get_active_eth_channels(local_chip_id), CoordSystem::TRANSLATED));
    return TTDevice::create(std::move(remote_communication));
}

// Create remote TTDevice from explicit EthCoord (rack, shelf, x, y). Does not set
// remote transfer ethernet cores; caller must call set_remote_transfer_ethernet_cores.
std::unique_ptr<TTDevice> create_remote_tt_device_from_coord(TTDevice *local_chip, int rack, int shelf, int x, int y) {
    EthCoord target_chip{0, x, y, rack, shelf};
    auto remote_communication = RemoteCommunication::create_remote_communication(local_chip, target_chip);
    if (!remote_communication) {
        UMD_THROW(
            error::RuntimeError,
            std::string("Remote communication is not supported for ") + arch_to_str(local_chip->get_arch()) +
                " architecture.");
    }
    return TTDevice::create(std::move(remote_communication));
}

void bind_tt_device(nb::module_ &m) {
    nb::enum_<IODeviceType>(m, "IODeviceType")
        .value("PCIe", IODeviceType::PCIe)
        .value("JTAG", IODeviceType::JTAG)
        .value("Undefined", IODeviceType::UNDEFINED);

    nb::exception<error::SigbusError>(m, "SigbusError");

    m.def(
        "raise_sigbus_error_for_testing",
        []() { throw error::SigbusError("This is a test exception from C++"); },
        release_gil(),
        "A helper function to verify SigbusError propagation");

    nb::class_<PciDeviceInfo>(m, "PciDeviceInfo")
        .def_ro("vendor_id", &PciDeviceInfo::vendor_id)
        .def_ro("device_id", &PciDeviceInfo::device_id)
        .def_ro("subsystem_vendor_id", &PciDeviceInfo::subsystem_vendor_id)
        .def_ro("subsystem_id", &PciDeviceInfo::subsystem_id)
        .def_ro("pci_domain", &PciDeviceInfo::pci_domain)
        .def_ro("pci_bus", &PciDeviceInfo::pci_bus)
        .def_ro("pci_device", &PciDeviceInfo::pci_device)
        .def_ro("pci_function", &PciDeviceInfo::pci_function)
        .def_ro("pci_bdf", &PciDeviceInfo::pci_bdf)
        .def("get_arch", &PciDeviceInfo::get_arch, release_gil());

    nb::class_<PCIDevice>(m, "PCIDevice")
        .def(nb::init<int>(), release_gil())
        .def_static(
            "enumerate_devices",
            []() { return PCIDevice::enumerate_devices(); },
            release_gil(),
            "Enumerates PCI devices.")
        .def_static(
            "enumerate_devices_info",
            []() { return PCIDevice::enumerate_devices_info(); },
            release_gil(),
            "Enumerates PCI device information.")
        .def("get_device_info", &PCIDevice::get_device_info, release_gil())
        .def("get_device_num", &PCIDevice::get_device_num, release_gil())
        .def_static(
            "read_kmd_version",
            &PCIDevice::read_kmd_version,
            release_gil(),
            "Read KMD version installed on the system.")
        .def_static(
            "read_device_info",
            &PCIDevice::read_device_info,
            nb::arg("fd"),
            release_gil(),
            "Read PCI device information.")
        .def_static(
            "is_arch_agnostic_reset_supported",
            &PCIDevice::is_arch_agnostic_reset_supported,
            release_gil(),
            "Check if KMD supports arch agnostic reset.");

    nb::class_<RemoteCommunication>(m, "RemoteCommunication")
        .def(
            "set_remote_transfer_ethernet_cores",
            [](RemoteCommunication &self, const std::vector<std::tuple<int, int>> &cores) {
                std::unordered_set<tt_xy_pair> xy_cores;
                for (const auto &core : cores) {
                    xy_cores.insert(
                        tt_xy_pair{static_cast<uint32_t>(std::get<0>(core)), static_cast<uint32_t>(std::get<1>(core))});
                }
                self.set_remote_transfer_ethernet_cores(xy_cores);
            },
            nb::arg("cores"),
            release_gil())
        .def("get_local_device", &RemoteCommunication::get_local_device, nb::rv_policy::reference_internal)
        .def(
            "get_remote_transfer_ethernet_core",
            [](RemoteCommunication &self) -> std::tuple<int, int> {
                tt_xy_pair core = self.get_remote_transfer_ethernet_core();
                return std::make_tuple(core.x, core.y);
            },
            release_gil());

    auto tt_device_class = nb::class_<TTDevice>(m, "TTDevice");

    nb::enum_<TTDevice::HangAction>(tt_device_class, "HangAction")
        .value("Throw", TTDevice::HangAction::THROW)
        .value("ReturnValue", TTDevice::HangAction::RETURN);

    tt_device_class
        .def_static(
            "create",
            static_cast<std::unique_ptr<TTDevice> (*)(
                int, IODeviceType, bool, const std::shared_ptr<SocArchDescriptor> &)>(&TTDevice::create),
            nb::arg("device_number"),
            nb::arg("device_type") = IODeviceType::PCIe,
            nb::arg("use_safe_api") = true,
            nb::arg("soc_arch_descriptor") = nullptr,
            nb::rv_policy::take_ownership,
            release_gil())
        .def("set_power_state", &TTDevice::set_power_state, nb::arg("busy"), release_gil())
        .def(
            "init_tt_device",
            &TTDevice::init_tt_device,
            nb::arg("timeout_ms") = timeout::ARC_STARTUP_TIMEOUT,
            release_gil())
        .def("get_soc_descriptor", &TTDevice::get_soc_descriptor, release_gil())
        .def("get_chip_info", &TTDevice::get_chip_info, release_gil())
        .def("get_arc_telemetry_reader", &TTDevice::get_arc_telemetry_reader, nb::rv_policy::reference_internal)
        .def("get_arch", &TTDevice::get_arch, release_gil())
        .def("get_board_id", &TTDevice::get_board_id, release_gil())
        .def("board_id", &TTDevice::get_board_id, release_gil())
        .def("get_board_type", &TTDevice::get_board_type, release_gil())
        .def("get_communication_device_type", &TTDevice::get_communication_device_type, release_gil())
        .def("get_communication_device_id", &TTDevice::get_communication_device_id, release_gil())
        .def("get_pci_device", &TTDevice::get_pci_device, nb::rv_policy::reference, release_gil())
        .def(
            "get_pci_interface_id",
            [](TTDevice &self) -> int {
                auto pci_device = self.get_pci_device();
                if (pci_device) {
                    return pci_device->get_device_num();
                } else {
                    return -1;
                }
            })
        .def("get_noc_translation_enabled", &TTDevice::get_noc_translation_enabled, release_gil())
        .def("is_remote", &TTDevice::is_remote, release_gil(), "Returns true if this is a remote TTDevice")
        .def("get_remote_communication", &TTDevice::get_remote_communication, nb::rv_policy::reference_internal)
        .def("get_firmware_info_provider", &TTDevice::get_firmware_info_provider, nb::rv_policy::reference_internal)
        // Compatibility with luwen's API - return self if arch matches, else None.
        .def(
            "as_wh",
            [](TTDevice &self) -> TTDevice * { return self.get_arch() == tt::ARCH::WORMHOLE_B0 ? &self : nullptr; },
            nb::rv_policy::reference_internal,
            "Return self if Wormhole, else None - for compatibility with luwen's API")
        .def(
            "as_bh",
            [](TTDevice &self) -> TTDevice * { return self.get_arch() == tt::ARCH::BLACKHOLE ? &self : nullptr; },
            nb::rv_policy::reference_internal,
            "Return self if Blackhole, else None - for compatibility with luwen's API")
        .def(
            "noc_read32",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr) -> uint32_t {
                tt_xy_pair core = {core_x, core_y};
                uint32_t value = 0;
                {
                    nb::gil_scoped_release release;
                    self.read_from_device(&value, core, addr, sizeof(uint32_t));
                }
                return value;
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            "Read a 32-bit value from a core at the specified address")
        .def(
            "noc_write32",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr, uint32_t value) -> void {
                tt_xy_pair core = {core_x, core_y};
                {
                    nb::gil_scoped_release release;
                    self.write_to_device(&value, core, addr, sizeof(uint32_t));
                }
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("value"),
            "Write a 32-bit value to a core at the specified address")
        .def(
            "noc_read",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr, size_t size) -> nb::bytes {
                tt_xy_pair core = {core_x, core_y};
                std::vector<uint8_t> buffer(size);
                {
                    nb::gil_scoped_release release;
                    self.read_from_device(buffer.data(), core, addr, size);
                }
                return nb::bytes(reinterpret_cast<const char *>(buffer.data()), buffer.size());
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("size"),
            "Read arbitrary-length data from a core at the specified address")
        .def(
            "noc_write",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr, nb::handle data) -> void {
                PyBufferView buffer(data);
                tt_xy_pair core = {core_x, core_y};
                {
                    nb::gil_scoped_release release;
                    self.write_to_device(buffer.readable_data(), core, addr, buffer.size());
                }
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("data"),
            nb::sig("def noc_write(self, core_x: int, core_y: int, addr: int, data: bytes | bytearray | memoryview) -> "
                    "None"),
            "Write arbitrary-length data to a core at the specified address")
        .def(
            "noc_broadcast",
            [](TTDevice &self, uint64_t addr, nb::handle data) -> void {
                PyBufferView buffer(data);
                {
                    nb::gil_scoped_release release;
                    self.noc_multicast_write(buffer.readable_data(), buffer.size(), addr);
                }
            },
            nb::arg("addr"),
            nb::arg("data"),
            nb::sig("def noc_broadcast(self, addr: int, data: bytes | bytearray | memoryview) -> None"),
            "Broadcast arbitrary-length data to all tensix cores on the chip at the specified address. data may be any "
            "buffer-protocol object (bytes, bytearray, memoryview, ...).")
        .def(
            "noc_broadcast32",
            [](TTDevice &self, uint64_t addr, uint32_t value) -> void {
                self.noc_multicast_write(&value, sizeof(uint32_t), addr);
            },
            nb::arg("addr"),
            nb::arg("value"),
            "Broadcast a 32-bit value to all tensix cores on the chip at the specified address")
        .def(
            "noc_multicast",
            [](TTDevice &self,
               uint32_t start_x,
               uint32_t start_y,
               uint32_t end_x,
               uint32_t end_y,
               uint64_t addr,
               nb::handle data) -> void {
                PyBufferView buffer(data);
                tt_xy_pair core_start = {start_x, start_y};
                tt_xy_pair core_end = {end_x, end_y};
                {
                    nb::gil_scoped_release release;
                    self.noc_multicast_write(buffer.readable_data(), buffer.size(), core_start, core_end, addr);
                }
            },
            nb::arg("start_x"),
            nb::arg("start_y"),
            nb::arg("end_x"),
            nb::arg("end_y"),
            nb::arg("addr"),
            nb::arg("data"),
            nb::sig("def noc_multicast(self, start_x: int, start_y: int, end_x: int, end_y: int, addr: int, data: "
                    "bytes | bytearray | memoryview) -> None"),
            "Broadcast arbitrary-length data to all cores in the rectangle [start, end] at the specified address. data "
            "may be any buffer-protocol object (bytes, bytearray, memoryview, ...).")
        .def(
            "noc_multicast32",
            [](TTDevice &self,
               uint32_t start_x,
               uint32_t start_y,
               uint32_t end_x,
               uint32_t end_y,
               uint64_t addr,
               uint32_t value) -> void {
                tt_xy_pair core_start = {start_x, start_y};
                tt_xy_pair core_end = {end_x, end_y};
                self.noc_multicast_write(&value, sizeof(uint32_t), core_start, core_end, addr);
            },
            nb::arg("start_x"),
            nb::arg("start_y"),
            nb::arg("end_x"),
            nb::arg("end_y"),
            nb::arg("addr"),
            nb::arg("value"),
            "Broadcast a 32-bit value to all cores in the rectangle [start, end] at the specified address")
        .def(
            "bar_read32",
            &TTDevice::bar_read32,
            nb::arg("addr"),
            release_gil(),
            "Read a 32-bit value from the specified address on bar0")
        .def(
            "bar_write32",
            &TTDevice::bar_write32,
            nb::arg("addr"),
            nb::arg("data"),
            release_gil(),
            "Write a 32-bit value to the specified address on bar0")
        .def(
            "is_pcie_hung",
            &TTDevice::is_pcie_hung,
            nb::arg("data_read") = HANG_READ_VALUE,
            nb::arg("action") = TTDevice::HangAction::THROW,
            release_gil(),
            "Check if the PCIe communication is hung.")
        .def(
            "is_noc_hung",
            &TTDevice::is_noc_hung,
            nb::arg("noc"),
            nb::arg("action") = TTDevice::HangAction::THROW,
            release_gil(),
            "Check if the specified NOC is hung.")
        .def(
            "get_risc_reset_state",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y) -> uint32_t {
                tt_xy_pair core = {core_x, core_y};
                return self.get_risc_reset_state(core);
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            release_gil(),
            "Get the raw soft reset register value for a core in translated coordinates. ")
        .def(
            "set_risc_reset_state",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint32_t soft_reset_raw_value) -> void {
                tt_xy_pair core = {core_x, core_y};
                self.set_risc_reset_state(core, soft_reset_raw_value);
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("soft_reset_raw_value"),
            release_gil(),
            "Set the raw soft reset register value for a core in translated coordinates. ")
        .def(
            "dma_read_from_device",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr, size_t size) -> nb::bytes {
                tt_xy_pair core = {core_x, core_y};
                std::vector<uint8_t> buffer(size);
                {
                    nb::gil_scoped_release release;
                    self.dma_read_from_device(buffer.data(), size, core, addr);
                }
                return nb::bytes(reinterpret_cast<const char *>(buffer.data()), buffer.size());
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("size"),
            "Read arbitrary-length data from a core at the specified address")
        .def(
            "dma_write_to_device",
            [](TTDevice &self, uint32_t core_x, uint32_t core_y, uint64_t addr, nb::handle data) -> void {
                PyBufferView buffer(data);
                tt_xy_pair core = {core_x, core_y};
                {
                    nb::gil_scoped_release release;
                    self.dma_write_to_device(buffer.readable_data(), buffer.size(), core, addr);
                }
            },
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("data"),
            nb::sig("def dma_write_to_device(self, core_x: int, core_y: int, addr: int, data: bytes | bytearray | "
                    "memoryview) -> None"),
            "Write arbitrary-length data to a core at the specified address")
        .def(
            "arc_msg",
            [](TTDevice &self,
               uint32_t msg_code,
               bool wait_for_done = true,
               std::vector<uint32_t> args = {},
               uint32_t timeout_ms = 1000) -> std::tuple<uint32_t, uint32_t, uint32_t> {
                // Warn if wait_for_done is False.
                if (!wait_for_done) {
                    log_warning(
                        tt::LogUMD, "arc_msg: wait_for_done=False is not respected. Message will wait for completion.");
                }
                // For Wormhole, prepend 0xaa00 to the msg_code.
                if (self.get_arch() == tt::ARCH::WORMHOLE_B0) {
                    msg_code = wormhole::ARC_MSG_COMMON_PREFIX | msg_code;
                }
                std::vector<uint32_t> return_values = {0, 0};
                uint32_t exit_code;
                {
                    nb::gil_scoped_release release;
                    exit_code = self.get_arc_messenger()->send_message(
                        msg_code, return_values, args, std::chrono::milliseconds(timeout_ms));
                }
                return std::make_tuple(exit_code, return_values[0], return_values[1]);
            },
            nb::arg("msg_code"),
            nb::arg("wait_for_done") = true,
            nb::arg("args") = std::vector<uint32_t>{},
            nb::arg("timeout_ms") = 1000,
            "Send ARC message and return (exit_code, return_3, return_4). "
            "Args is a list of uint32_t arguments. For Wormhole, max 2 args (each <= 0xFFFF). For Blackhole, max 7 "
            "args. Timeout is in milliseconds.")
        .def(
            "arc_msg",
            [](TTDevice &self,
               uint32_t msg_code,
               bool wait_for_done,
               // Default to 0xFFFF: packed as (arg0 | arg1 << 16), the firmware treats the combined
               // value 0xFFFFFFFF as a sentinel meaning "no argument provided".
               uint32_t arg0,
               uint32_t arg1 = 0xffff,
               uint32_t timeout_ms = 1000) -> std::tuple<uint32_t, uint32_t, uint32_t> {
                // Warn if wait_for_done is False.
                if (!wait_for_done) {
                    log_warning(
                        tt::LogUMD, "arc_msg: wait_for_done=False is not respected. Message will wait for completion.");
                }
                // For Wormhole, prepend 0xaa00 to the msg_code.
                if (self.get_arch() == tt::ARCH::WORMHOLE_B0) {
                    msg_code = wormhole::ARC_MSG_COMMON_PREFIX | msg_code;
                }
                std::vector<uint32_t> args = {arg0, arg1};
                std::vector<uint32_t> return_values = {0, 0};
                uint32_t exit_code;
                {
                    nb::gil_scoped_release release;
                    exit_code = self.get_arc_messenger()->send_message(
                        msg_code, return_values, args, std::chrono::milliseconds(timeout_ms));
                }
                return std::make_tuple(exit_code, return_values[0], return_values[1]);
            },
            nb::arg("msg_code"),
            nb::arg("wait_for_done") = true,
            nb::arg("arg0") = 0xffff,
            nb::arg("arg1") = 0xffff,
            nb::arg("timeout_ms") = 1000,
            "Send ARC message with two arguments and return (exit_code, return_3, return_4). Timeout is in "
            "milliseconds.")
        .def(
            "arc_msg",
            [](TTDevice &self,
               uint32_t msg_code,
               bool wait_for_done = true,
               // Default to 0xFFFF: packed as (arg0 | arg1 << 16), the firmware treats the combined
               // value 0xFFFFFFFF as a sentinel meaning "no argument provided".
               uint32_t arg0 = 0xffff,
               uint32_t arg1 = 0xffff,
               uint32_t timeout = 1) -> std::tuple<uint32_t, uint32_t, uint32_t> {
                // Warn if wait_for_done is False.
                if (!wait_for_done) {
                    log_warning(
                        tt::LogUMD, "arc_msg: wait_for_done=False is not respected. Message will wait for completion.");
                }
                // For Wormhole, prepend 0xaa00 to the msg_code.
                if (self.get_arch() == tt::ARCH::WORMHOLE_B0) {
                    msg_code = wormhole::ARC_MSG_COMMON_PREFIX | msg_code;
                }
                std::vector<uint32_t> args = {arg0, arg1};
                std::vector<uint32_t> return_values = {0, 0};
                uint32_t exit_code;
                {
                    nb::gil_scoped_release release;
                    exit_code = self.get_arc_messenger()->send_message(
                        msg_code, return_values, args, std::chrono::milliseconds(timeout * 1000));
                }
                return std::make_tuple(exit_code, return_values[0], return_values[1]);
            },
            nb::arg("msg_code"),
            nb::arg("wait_for_done") = true,
            nb::arg("arg0") = 0xffff,
            nb::arg("arg1") = 0xffff,
            nb::arg("timeout") = 1,
            "Send ARC message with two arguments and return (exit_code, return_3, return_4). Timeout is in seconds.")
        // ---------------------------------------------------------------------------
        // noc_id variants — temporary until noc_id is added to the main read/write
        // interface in TTDevice. All functions below accept noc_id as the first
        // argument and throw if it is not 0.
        // ---------------------------------------------------------------------------
        .def(
            "noc_read",
            [](TTDevice &self, uint32_t noc_id, uint32_t core_x, uint32_t core_y, uint64_t addr, nb::handle buffer)
                -> void {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0");
                }
                PyBufferView view(buffer, /*writable=*/true);
                void *data_ptr = view.writable_data();
                size_t data_size = view.size();
                tt_xy_pair core = {core_x, core_y};
                {
                    nb::gil_scoped_release release;
                    self.read_from_device(data_ptr, core, addr, data_size);
                }
            },
            nb::arg("noc_id"),
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("buffer"),
            nb::sig("def noc_read(self, noc_id: int, core_x: int, core_y: int, addr: int, buffer: bytearray | "
                    "memoryview) -> None"),
            "Read data into the provided buffer from a core at the specified address. noc_id must be 0 for now. buffer "
            "must be a writable buffer-protocol object (bytearray, writable memoryview, ...).")
        .def(
            "dma_read_from_device",
            [](TTDevice &self, uint32_t noc_id, uint32_t core_x, uint32_t core_y, uint64_t addr, nb::handle buffer)
                -> void {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0.");
                }
                PyBufferView view(buffer, /*writable=*/true);
                void *data_ptr = view.writable_data();
                size_t data_size = view.size();
                tt_xy_pair core = {core_x, core_y};
                {
                    nb::gil_scoped_release release;
                    self.dma_read_from_device(data_ptr, data_size, core, addr);
                }
            },
            nb::arg("noc_id"),
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("buffer"),
            nb::sig(
                "def dma_read_from_device(self, noc_id: int, core_x: int, core_y: int, addr: int, buffer: bytearray "
                "| memoryview) -> None"),
            "Read data into the provided buffer from a core at the specified address. noc_id must be 0 for now. buffer "
            "must be a writable buffer-protocol object (bytearray, writable memoryview, ...).")
        .def(
            "noc_read32",
            [](TTDevice &self, uint32_t noc_id, uint32_t core_x, uint32_t core_y, uint64_t addr) -> uint32_t {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0.");
                }
                tt_xy_pair core = {core_x, core_y};
                uint32_t value = 0;
                self.read_from_device(&value, core, addr, sizeof(uint32_t));
                return value;
            },
            nb::arg("noc_id"),
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            "Read a 32-bit value from a core at the specified address. noc_id must be 0 for now.")
        .def(
            "noc_write",
            [](TTDevice &self, uint32_t noc_id, uint32_t core_x, uint32_t core_y, uint64_t addr, nb::handle data)
                -> void {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0.");
                }
                PyBufferView buffer(data);
                tt_xy_pair core = {core_x, core_y};
                {
                    nb::gil_scoped_release release;
                    self.write_to_device(buffer.readable_data(), core, addr, buffer.size());
                }
            },
            nb::arg("noc_id"),
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("data"),
            nb::sig("def noc_write(self, noc_id: int, core_x: int, core_y: int, addr: int, data: bytes | bytearray | "
                    "memoryview) -> None"),
            "Write arbitrary-length data to a core at the specified address. noc_id must be 0 for now.")
        .def(
            "noc_write32",
            [](TTDevice &self, uint32_t noc_id, uint32_t core_x, uint32_t core_y, uint64_t addr, uint32_t value)
                -> void {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0.");
                }
                tt_xy_pair core = {core_x, core_y};
                self.write_to_device(&value, core, addr, sizeof(uint32_t));
            },
            nb::arg("noc_id"),
            nb::arg("core_x"),
            nb::arg("core_y"),
            nb::arg("addr"),
            nb::arg("value"),
            "Write a 32-bit value to a core at the specified address. noc_id must be 0 for now.")
        .def(
            "noc_broadcast",
            [](TTDevice &self, uint32_t noc_id, uint64_t addr, nb::handle data) -> void {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0.");
                }
                PyBufferView buffer(data);
                {
                    nb::gil_scoped_release release;
                    self.noc_multicast_write(buffer.readable_data(), buffer.size(), addr);
                }
            },
            nb::arg("noc_id"),
            nb::arg("addr"),
            nb::arg("data"),
            nb::sig("def noc_broadcast(self, noc_id: int, addr: int, data: bytes | bytearray | memoryview) -> None"),
            "Broadcast arbitrary-length data to all cores on the chip at the specified address. noc_id must be 0 for "
            "now. data may be any buffer-protocol object (bytes, bytearray, memoryview, ...).")
        .def(
            "noc_broadcast32",
            [](TTDevice &self, uint32_t noc_id, uint64_t addr, uint32_t value) -> void {
                if (noc_id != 0) {
                    UMD_THROW(error::RuntimeError, "noc_id must be 0.");
                }
                self.noc_multicast_write(&value, sizeof(uint32_t), addr);
            },
            nb::arg("noc_id"),
            nb::arg("addr"),
            nb::arg("value"),
            "Broadcast a 32-bit value to all cores on the chip at the specified address. noc_id must be 0 for now.");

    nb::class_<SPITTDevice>(m, "SPITTDevice")
        .def_static(
            "create",
            [](TTDevice &device) { return SPITTDevice::create(&device); },
            nb::arg("device"),
            nb::rv_policy::take_ownership,
            release_gil(),
            "Create an SPITTDevice for the given TTDevice (factory method that returns architecture-specific "
            "implementation)")
        .def(
            "read",
            [](SPITTDevice &self, uint32_t addr, nb::bytearray data) -> void {
                uint8_t *data_ptr = reinterpret_cast<uint8_t *>(data.data());
                size_t data_size = data.size();
                {
                    nb::gil_scoped_release release;
                    self.read(addr, data_ptr, data_size);
                }
            },
            nb::arg("addr"),
            nb::arg("data"),
            "Read data from SPI flash memory")
        .def(
            "write",
            [](SPITTDevice &self, uint32_t addr, nb::handle data, bool skip_write_to_spi = false) -> void {
                PyBufferView buffer(data);
                {
                    nb::gil_scoped_release release;
                    self.write(
                        addr, static_cast<const uint8_t *>(buffer.readable_data()), buffer.size(), skip_write_to_spi);
                }
            },
            nb::arg("addr"),
            nb::arg("data"),
            nb::arg("skip_write_to_spi") = false,
            nb::sig("def write(self, addr: int, data: bytes | bytearray | memoryview, skip_write_to_spi: bool = False) "
                    "-> None"),
            "Write data to SPI flash memory. If skip_write_to_spi is True, only writes to buffer without committing to "
            "SPI. data may be any buffer-protocol object (bytes, bytearray, memoryview, ...).")
        .def(
            "get_spi_fw_bundle_version",
            &SPITTDevice::get_spi_fw_bundle_version,
            release_gil(),
            "Get firmware bundle version from SPI (Blackhole only). "
            "Returns raw 32-bit value with format [component][major][minor][patch] (each 8 bits).");

#ifdef TT_UMD_BUILD_SIMULATION
    // Add simulation TTDevice factory binding - must be inside TT_UMD_BUILD_SIMULATION guard.
    m.def(
        "create_simulation_tt_device",
        static_cast<std::unique_ptr<TTDevice> (*)(const std::filesystem::path &, int, bool)>(
            &create_simulation_tt_device),
        nb::arg("simulator_path"),
        nb::arg("num_host_mem_channels") = 0,
        nb::arg("copy_sim_binary") = false,
        nb::rv_policy::take_ownership,
        release_gil(),
        "Creates a simulation TTDevice from a simulator path. "
        "If the path ends with '.so', creates a TTSimTTDevice (functional simulator). "
        "Otherwise, creates an RtlSimulationTTDevice (RTL simulator).");

    nb::class_<TTSimTTDevice, TTDevice>(m, "TTSimTTDevice")
        .def_static(
            "create",
            &TTSimTTDevice::create,
            nb::arg("simulator_directory"),
            nb::arg("num_host_mem_channels") = 0,
            nb::arg("copy_sim_binary") = false,
            release_gil(),
            "Creates a TTSimTTDevice for functional simulation communication.")
        .def(
            "assert_risc_reset",
            &TTSimTTDevice::assert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            release_gil(),
            "Assert RISC reset for selected RISC cores on a given core.")
        .def(
            "deassert_risc_reset",
            &TTSimTTDevice::deassert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            nb::arg("staggered_start") = false,
            release_gil(),
            "Deassert RISC reset for selected RISC cores on a given core.")
        .def(
            "get_soc_descriptor",
            &TTSimTTDevice::get_soc_descriptor,
            nb::rv_policy::reference_internal,
            "Get the SocDescriptor associated with this simulation device.")

        .def("get_clock", &TTSimTTDevice::get_clock, release_gil(), "Get the clock frequency.")
        .def(
            "get_min_clock_freq", &TTSimTTDevice::get_min_clock_freq, release_gil(), "Get the minimum clock frequency.")
        .def_rw("bar0_base", &TTSimTTDevice::bar0_base, "Base address for BAR0.")
        .def_rw("bar4_base", &TTSimTTDevice::bar4_base, "Base address for BAR4.");

    nb::class_<RtlSimulationTTDevice, TTDevice>(m, "RtlSimulationTTDevice")
        .def_static(
            "create",
            &RtlSimulationTTDevice::create,
            nb::arg("simulator_directory"),
            nb::arg("num_host_mem_channels") = 0,
            release_gil(),
            "Creates an RtlSimulationTTDevice for RTL simulation communication.")
        .def(
            "assert_risc_reset",
            &RtlSimulationTTDevice::assert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            release_gil(),
            "Assert RISC reset for selected RISC cores on a given core.")
        .def(
            "deassert_risc_reset",
            &RtlSimulationTTDevice::deassert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            nb::arg("staggered_start") = false,
            release_gil(),
            "Deassert RISC reset for selected RISC cores on a given core.")
        .def(
            "get_soc_descriptor",
            &RtlSimulationTTDevice::get_soc_descriptor,
            nb::rv_policy::reference_internal,
            release_gil(),
            "Get the SocDescriptor associated with this RTL simulation device.");
#endif

    m.def(
        "create_remote_tt_device",
        &create_remote_tt_device,
        nb::arg("local_chip"),
        nb::arg("cluster_descriptor"),
        nb::arg("remote_chip_id"),
        nb::rv_policy::take_ownership,
        release_gil(),
        "Creates a remote TTDevice for communication with a remote chip.");

    // Keep the old name as an alias for backwards compatibility.
    m.def(
        "create_remote_wormhole_tt_device",
        &create_remote_tt_device,
        nb::arg("local_chip"),
        nb::arg("cluster_descriptor"),
        nb::arg("remote_chip_id"),
        nb::rv_policy::take_ownership,
        release_gil(),
        "Deprecated: use create_remote_tt_device instead.");

    m.def(
        "create_remote_tt_device_from_coord",
        &create_remote_tt_device_from_coord,
        nb::arg("local_chip"),
        nb::arg("rack"),
        nb::arg("shelf"),
        nb::arg("x"),
        nb::arg("y"),
        nb::rv_policy::take_ownership,
        release_gil(),
        "Creates a remote TTDevice for communication with a remote chip at (rack, shelf, x, y). "
        "Does not set remote transfer ethernet cores; caller must set them explicitly.");

    // Keep the old name as an alias for backwards compatibility.
    m.def(
        "create_remote_wormhole_tt_device_from_coord",
        &create_remote_tt_device_from_coord,
        nb::arg("local_chip"),
        nb::arg("rack"),
        nb::arg("shelf"),
        nb::arg("x"),
        nb::arg("y"),
        nb::rv_policy::take_ownership,
        release_gil(),
        "Deprecated: use create_remote_tt_device_from_coord instead.");
}
