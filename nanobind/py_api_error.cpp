// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "umd/device/utils/error.hpp"
#include "umd/device/utils/error_detail.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;
using namespace tt::umd::error;

void bind_error(nb::module_ &m) {
    nb::module_ error_module = m.def_submodule("error", "UMD error types and exceptions");

    // Base exception class.
    static nb::exception<UmdBaseException> umd_base_exception(error_module, "UmdBaseException");

    // NoData struct for errors without metadata.
    nb::class_<NoData>(error_module, "NoData").def(nb::init<>());

    // RuntimeError.
    nb::class_<RuntimeError>(error_module, "RuntimeError")
        .def(nb::init<const std::string &>(), nb::arg("message"))
        .def("message", nb::overload_cast<>(&RuntimeError::message, nb::const_), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&RuntimeError::data, nb::const_), "Get the error data");

    // TTDeviceData.
    nb::class_<TTDeviceData>(error_module, "TTDeviceData")
        .def(nb::init<>())
        .def_rw("io_device_type", &TTDeviceData::io_device_type)
        .def_rw("chip_id", &TTDeviceData::chip_id)
        .def_rw("arch", &TTDeviceData::arch)
        .def_rw("discovery_unique_id", &TTDeviceData::discovery_unique_id);

    // DeviceCoreData.
    nb::class_<DeviceCoreData, TTDeviceData>(error_module, "DeviceCoreData")
        .def(nb::init<>())
        .def_rw("core", &DeviceCoreData::core)
        .def_rw("noc_id", &DeviceCoreData::noc_id);

    // ArcStartupData.
    nb::class_<ArcStartupData, DeviceCoreData>(error_module, "ArcStartupData")
        .def(nb::init<>())
        .def_rw("scratch_status", &ArcStartupData::scratch_status)
        .def_rw("postcode", &ArcStartupData::postcode)
        .def_rw("message_id", &ArcStartupData::message_id);

    // ArcStartupError.
    nb::class_<ArcStartupError>(error_module, "ArcStartupError")
        .def("message", nb::overload_cast<>(&ArcStartupError::message, nb::const_), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&ArcStartupError::data, nb::const_), "Get the error data");

    // NocHangData.
    nb::class_<NocHangData, TTDeviceData>(error_module, "NocHangData")
        .def(nb::init<>())
        .def_rw("noc_id", &NocHangData::noc_id);

    // NocHangError.
    nb::class_<NocHangError>(error_module, "NocHangError")
        .def("message", nb::overload_cast<>(&NocHangError::message, nb::const_), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&NocHangError::data, nb::const_), "Get the error data");

    // PcieHangData.
    nb::class_<PcieHangData, TTDeviceData>(error_module, "PcieHangData")
        .def(nb::init<>())
        .def_rw("data_read", &PcieHangData::data_read);

    // PcieHangError.
    nb::class_<PcieHangError>(error_module, "PcieHangError")
        .def("message", nb::overload_cast<>(&PcieHangError::message, nb::const_), "Get the error message")
        .def_prop_ro("data", nb::overload_cast<>(&PcieHangError::data, nb::const_), "Get the error data");
}
