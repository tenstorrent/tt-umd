// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <chrono>

#include "umd/device/topology/topology_discovery_error.hpp"
#include "umd/device/tt_device/tt_device_error.hpp"
#include "umd/device/utils/error.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::error;

void bind_error(nb::module_ &m) {
    nb::module_ error_module = m.def_submodule("error", "UMD error types and exceptions");

    // Base exception class.
    static nb::exception<UmdBaseException> umd_base_exception(error_module, "UmdBaseException");

    // DeviceTimeoutError: a critical, catchable exception so Python callers can recover from a host-side
    // MMIO timeout rather than crash. It is thrown wrapped as UmdException<DeviceTimeoutError>; registering
    // it after the base, as a UmdBaseException subclass, makes its translator take precedence, so both
    // `except error.DeviceTimeoutError` and `except error.UmdBaseException` catch it.
    nb::exception<UmdException<DeviceTimeoutError>>(error_module, "DeviceTimeoutError", umd_base_exception);
    error_module.def(
        "raise_device_timeout_error_for_testing",
        []() {
            UMD_THROW(DeviceTimeoutError, "store", 4, std::chrono::nanoseconds(0), std::chrono::milliseconds(0), 0, 0);
        },
        release_gil(),
        "Raise a DeviceTimeoutError from C++ to verify it propagates to Python.");

    // NoData struct for errors without metadata.
    nb::class_<NoData>(error_module, "NoData").def(nb::init<>(), release_gil());

    // RuntimeError.
    nb::class_<RuntimeError>(error_module, "RuntimeError")
        .def(nb::init<const std::string &>(), nb::arg("message"), release_gil())
        .def("message", nb::overload_cast<>(&RuntimeError::message, nb::const_), release_gil(), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&RuntimeError::data, nb::const_), "Get the error data");

    // TTDeviceData.
    nb::class_<TTDeviceData>(error_module, "TTDeviceData")
        .def(nb::init<>(), release_gil())
        .def_rw("io_device_type", &TTDeviceData::io_device_type)
        .def_rw("chip_id", &TTDeviceData::chip_id)
        .def_rw("arch", &TTDeviceData::arch)
        .def_rw("discovery_unique_id", &TTDeviceData::discovery_unique_id);

    // DeviceCoreData.
    nb::class_<DeviceCoreData, TTDeviceData>(error_module, "DeviceCoreData")
        .def(nb::init<>(), release_gil())
        .def_rw("core", &DeviceCoreData::core)
        .def_rw("noc_id", &DeviceCoreData::noc_id);

    // ArcStartupData.
    nb::class_<ArcStartupData, DeviceCoreData>(error_module, "ArcStartupData")
        .def(nb::init<>(), release_gil())
        .def_rw("scratch_status", &ArcStartupData::scratch_status)
        .def_rw("postcode", &ArcStartupData::postcode)
        .def_rw("message_id", &ArcStartupData::message_id)
        .def_rw("smc_init_status", &ArcStartupData::smc_init_status);

    // ArcStartupError.
    nb::class_<ArcStartupError>(error_module, "ArcStartupError")
        .def(
            "message",
            nb::overload_cast<>(&ArcStartupError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&ArcStartupError::data, nb::const_), "Get the error data");

    // NocHangData.
    nb::class_<NocHangData, TTDeviceData>(error_module, "NocHangData")
        .def(nb::init<>(), release_gil())
        .def_rw("noc_id", &NocHangData::noc_id);

    // NocHangError.
    nb::class_<NocHangError>(error_module, "NocHangError")
        .def("message", nb::overload_cast<>(&NocHangError::message, nb::const_), release_gil(), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&NocHangError::data, nb::const_), "Get the error data");

    // PcieHangData.
    nb::class_<PcieHangData, TTDeviceData>(error_module, "PcieHangData")
        .def(nb::init<>(), release_gil())
        .def_rw("data_read", &PcieHangData::data_read);

    // PcieHangError.
    nb::class_<PcieHangError>(error_module, "PcieHangError")
        .def(
            "message", nb::overload_cast<>(&PcieHangError::message, nb::const_), release_gil(), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&PcieHangError::data, nb::const_), "Get the error data");

    // UninitializedDeviceError. Its data is a plain TTDeviceData (no dedicated data struct).
    nb::class_<UninitializedDeviceError>(error_module, "UninitializedDeviceError")
        .def(
            "message",
            nb::overload_cast<>(&UninitializedDeviceError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&UninitializedDeviceError::data, nb::const_), "Get the error data");

    // UnresolvableCoordinateError. Its data is a plain DeviceCoreData (no dedicated data struct).
    nb::class_<UnresolvableCoordinateError>(error_module, "UnresolvableCoordinateError")
        .def(
            "message",
            nb::overload_cast<>(&UnresolvableCoordinateError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&UnresolvableCoordinateError::data, nb::const_), "Get the error data");

    // UnsupportedCMFWData.
    nb::class_<UnsupportedCMFWData, TTDeviceData>(error_module, "UnsupportedCMFWData")
        .def(nb::init<>(), release_gil())
        .def_rw("found", &UnsupportedCMFWData::found)
        .def_rw("minimum", &UnsupportedCMFWData::minimum);

    // UnsupportedCMFWError.
    nb::class_<UnsupportedCMFWError>(error_module, "UnsupportedCMFWError")
        .def(
            "message",
            nb::overload_cast<>(&UnsupportedCMFWError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&UnsupportedCMFWError::data, nb::const_), "Get the error data");

    // CMFWMismatchData.
    nb::class_<CMFWMismatchData, TTDeviceData>(error_module, "CMFWMismatchData")
        .def(nb::init<>(), release_gil())
        .def_rw("expected", &CMFWMismatchData::expected)
        .def_rw("found", &CMFWMismatchData::found);

    // CMFWMismatchError.
    nb::class_<CMFWMismatchError>(error_module, "CMFWMismatchError")
        .def(
            "message",
            nb::overload_cast<>(&CMFWMismatchError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&CMFWMismatchError::data, nb::const_), "Get the error data");

    // EthFirmwareMismatchData.
    nb::class_<EthFirmwareMismatchData, DeviceCoreData>(error_module, "EthFirmwareMismatchData")
        .def(nb::init<>(), release_gil())
        .def_rw("expected", &EthFirmwareMismatchData::expected)
        .def_rw("found", &EthFirmwareMismatchData::found);

    // EthFirmwareMismatchError.
    nb::class_<EthFirmwareMismatchError>(error_module, "EthFirmwareMismatchError")
        .def(
            "message",
            nb::overload_cast<>(&EthFirmwareMismatchError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&EthFirmwareMismatchError::data, nb::const_), "Get the error data");

    // UnexpectedRoutingFirmwareConfigData.
    nb::class_<UnexpectedRoutingFirmwareConfigData, DeviceCoreData>(error_module, "UnexpectedRoutingFirmwareConfigData")
        .def(nb::init<>(), release_gil())
        .def_rw("expected", &UnexpectedRoutingFirmwareConfigData::expected)
        .def_rw("found", &UnexpectedRoutingFirmwareConfigData::found);

    // UnexpectedRoutingFirmwareConfigError.
    nb::class_<UnexpectedRoutingFirmwareConfigError>(error_module, "UnexpectedRoutingFirmwareConfigError")
        .def(
            "message",
            nb::overload_cast<>(&UnexpectedRoutingFirmwareConfigError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro(
            "data", nb::overload_cast<>(&UnexpectedRoutingFirmwareConfigError::data, nb::const_), "Get the error data");

    // EthFirmwareHeartbeatData.
    nb::class_<EthFirmwareHeartbeatData, DeviceCoreData>(error_module, "EthFirmwareHeartbeatData")
        .def(nb::init<>(), release_gil())
        .def_rw("value", &EthFirmwareHeartbeatData::value);

    // EthFirmwareHeartbeatError.
    nb::class_<EthFirmwareHeartbeatError>(error_module, "EthFirmwareHeartbeatError")
        .def(
            "message",
            nb::overload_cast<>(&EthFirmwareHeartbeatError::message, nb::const_),
            release_gil(),
            "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&EthFirmwareHeartbeatError::data, nb::const_), "Get the error data");
}
