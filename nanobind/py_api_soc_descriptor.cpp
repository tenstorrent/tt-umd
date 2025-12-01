/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

void bind_soc_descriptor(nb::module_ &m) {
    // Bind CoreType enum
    nb::enum_<CoreType>(m, "CoreType")
        .value("ARC", CoreType::ARC)
        .value("DRAM", CoreType::DRAM)
        .value("ACTIVE_ETH", CoreType::ACTIVE_ETH)
        .value("IDLE_ETH", CoreType::IDLE_ETH)
        .value("PCIE", CoreType::PCIE)
        .value("TENSIX", CoreType::TENSIX)
        .value("ROUTER_ONLY", CoreType::ROUTER_ONLY)
        .value("SECURITY", CoreType::SECURITY)
        .value("L2CPU", CoreType::L2CPU)
        .value("HARVESTED", CoreType::HARVESTED)
        .value("ETH", CoreType::ETH)
        .value("WORKER", CoreType::WORKER)
        .def("__str__", [](CoreType ct) { return to_str(ct); })
        .def("__repr__", [](CoreType ct) { return "CoreType." + to_str(ct); });

    // Bind CoordSystem enum
    nb::enum_<CoordSystem>(m, "CoordSystem")
        .value("LOGICAL", CoordSystem::LOGICAL)
        .value("NOC0", CoordSystem::NOC0)
        .value("TRANSLATED", CoordSystem::TRANSLATED)
        .value("NOC1", CoordSystem::NOC1)
        .def("__str__", [](CoordSystem cs) { return to_str(cs); })
        .def("__repr__", [](CoordSystem cs) { return "CoordSystem." + to_str(cs); });

    // Bind CoreCoord struct
    nb::class_<CoreCoord>(m, "CoreCoord")
        .def(
            nb::init<size_t, size_t, CoreType, CoordSystem>(),
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("core_type"),
            nb::arg("coord_system"))
        .def_ro("x", &CoreCoord::x)
        .def_ro("y", &CoreCoord::y)
        .def_ro("core_type", &CoreCoord::core_type)
        .def_ro("coord_system", &CoreCoord::coord_system)
        .def("__str__", &CoreCoord::str)
        .def("__repr__", [](const CoreCoord &cc) { return cc.str(); })
        .def("__eq__", &CoreCoord::operator==)
        .def("__lt__", &CoreCoord::operator<);

    // Bind SocDescriptor class
    nb::class_<SocDescriptor>(m, "SocDescriptor")
        .def(
            "__init__",
            [](SocDescriptor *soc_desc, TTDevice &tt_device) {
                new (soc_desc) SocDescriptor(tt_device.get_arch(), tt_device.get_chip_info());
            },
            nb::arg("tt_device"),
            "Create a SocDescriptor from a TTDevice")
        .def(
            "get_cores",
            &SocDescriptor::get_cores,
            nb::arg("core_type"),
            nb::arg("coord_system") = CoordSystem::NOC0,
            nb::arg("channel") = std::nullopt,
            "Get all cores of a specific type in the specified coordinate system")
        .def(
            "get_harvested_cores",
            &SocDescriptor::get_harvested_cores,
            nb::arg("core_type"),
            nb::arg("coord_system") = CoordSystem::NOC0,
            "Get all harvested cores of a specific type in the specified coordinate system")
        .def(
            "get_all_cores",
            &SocDescriptor::get_all_cores,
            nb::arg("coord_system") = CoordSystem::NOC0,
            "Get all cores in the specified coordinate system")
        .def(
            "get_all_harvested_cores",
            &SocDescriptor::get_all_harvested_cores,
            nb::arg("coord_system") = CoordSystem::NOC0,
            "Get all harvested cores in the specified coordinate system")
        .def(
            "serialize_to_file",
            [](const SocDescriptor &self, const std::string &dest_file) -> std::string {
                std::filesystem::path file_path = self.serialize_to_file(dest_file);
                return file_path.string();
            },
            nb::arg("dest_file") = "",
            "Serialize the soc descriptor to a YAML file")
        .def(
            "translate_coord_to",
            nb::overload_cast<const CoreCoord, const CoordSystem>(&SocDescriptor::translate_coord_to, nb::const_),
            nb::arg("core_coord"),
            nb::arg("coord_system"),
            "Translate a CoreCoord to the specified coordinate system")
        .def(
            "translate_coord_to",
            nb::overload_cast<const tt_xy_pair, const CoordSystem, const CoordSystem>(
                &SocDescriptor::translate_coord_to, nb::const_),
            nb::arg("core_location"),
            nb::arg("input_coord_system"),
            nb::arg("target_coord_system"),
            "Translate a tt_xy_pair from one coordinate system to another");
}
