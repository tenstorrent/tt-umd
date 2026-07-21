// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/soc_arch_descriptor.hpp"
#include "umd/device/soc_descriptor.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/core_coordinates.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt;
using namespace tt::umd;

void bind_soc_descriptor(nb::module_ &m) {
    // Bind CoreType enum.
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
        .value("UNSPECIFIED", CoreType::UNSPECIFIED)
        .def(
            "__str__", [](CoreType ct) { return to_str(ct); }, release_gil())
        .def(
            "__repr__", [](CoreType ct) { return "CoreType." + to_str(ct); }, release_gil());

    // Bind CoordSystem enum.
    nb::enum_<CoordSystem>(m, "CoordSystem")
        .value("LOGICAL", CoordSystem::LOGICAL)
        .value("NOC0", CoordSystem::NOC0)
        .value("TRANSLATED", CoordSystem::TRANSLATED)
        .value("NOC1", CoordSystem::NOC1)
        .value("LITERAL", CoordSystem::LITERAL)
        .def(
            "__str__", [](CoordSystem cs) { return to_str(cs); }, release_gil())
        .def(
            "__repr__", [](CoordSystem cs) { return "CoordSystem." + to_str(cs); }, release_gil());

    // Bind CoreCoord struct.
    nb::class_<CoreCoord>(m, "CoreCoord")
        .def(
            nb::init<size_t, size_t, CoreType, CoordSystem>(),
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("core_type"),
            nb::arg("coord_system"),
            release_gil())
        .def_ro("x", &CoreCoord::x)
        .def_ro("y", &CoreCoord::y)
        .def_ro("core_type", &CoreCoord::core_type)
        .def_ro("coord_system", &CoreCoord::coord_system)
        .def("__str__", &CoreCoord::str, release_gil())
        .def(
            "__repr__", [](const CoreCoord &cc) { return cc.str(); }, release_gil())
        .def("__eq__", &CoreCoord::operator==, release_gil())
        .def("__lt__", &CoreCoord::operator<, release_gil());

    nb::class_<SocArchDescriptor>(m, "SocArchDescriptor")
        .def(
            nb::init<tt::ARCH>(),
            nb::arg("arch"),
            release_gil(),
            "Create a SocArchDescriptor from an architecture enum")
        .def(
            nb::init<const std::string &>(),
            nb::arg("soc_descriptor_path"),
            release_gil(),
            "Create a SocArchDescriptor from a YAML SoC descriptor file")
        .def_prop_ro("arch", &SocArchDescriptor::get_arch, release_gil())
        .def_prop_ro("grid_size", &SocArchDescriptor::get_grid_size, release_gil())
        .def_prop_ro("tensix_cores", &SocArchDescriptor::get_tensix_cores, release_gil())
        .def_prop_ro("dram_cores", &SocArchDescriptor::get_dram_cores, release_gil())
        .def_prop_ro("eth_cores", &SocArchDescriptor::get_eth_cores, release_gil())
        .def_prop_ro("firmware_cores", &SocArchDescriptor::get_firmware_cores, release_gil())
        .def_prop_ro("pcie_cores", &SocArchDescriptor::get_pcie_cores, release_gil())
        .def_prop_ro("router_cores", &SocArchDescriptor::get_router_cores, release_gil())
        .def_prop_ro("security_cores", &SocArchDescriptor::get_security_cores, release_gil())
        .def_prop_ro("l2cpu_cores", &SocArchDescriptor::get_l2cpu_cores, release_gil())
        .def_prop_ro("dispatch_cores", &SocArchDescriptor::get_dispatch_cores, release_gil())
        .def_prop_ro("worker_l1_size", &SocArchDescriptor::get_worker_l1_size, release_gil())
        .def_prop_ro("eth_l1_size", &SocArchDescriptor::get_eth_l1_size, release_gil())
        .def_prop_ro("dram_bank_size", &SocArchDescriptor::get_dram_bank_size, release_gil())
        .def_prop_ro("noc0_x_to_noc1_x", &SocArchDescriptor::get_noc0_x_to_noc1_x, release_gil())
        .def_prop_ro("noc0_y_to_noc1_y", &SocArchDescriptor::get_noc0_y_to_noc1_y, release_gil())
        .def_prop_ro("overlay_version", &SocArchDescriptor::get_overlay_version, release_gil())
        .def_prop_ro("unpacker_version", &SocArchDescriptor::get_unpacker_version, release_gil())
        .def_prop_ro("dst_size_alignment", &SocArchDescriptor::get_dst_size_alignment, release_gil())
        .def_prop_ro("packer_version", &SocArchDescriptor::get_packer_version, release_gil())
        .def_prop_ro("trisc_sizes", &SocArchDescriptor::get_trisc_sizes, release_gil())
        .def_prop_ro("device_descriptor_file_path", &SocArchDescriptor::get_device_descriptor_file_path, release_gil())
        .def_prop_ro("worker_grid_size", &SocArchDescriptor::get_worker_grid_size, release_gil())
        .def_static(
            "get_arch_from_path",
            &SocArchDescriptor::get_arch_from_path,
            nb::arg("soc_descriptor_path"),
            release_gil(),
            "Get architecture from a YAML SoC descriptor file path")
        .def_static(
            "get_grid_size_from_path",
            &SocArchDescriptor::get_grid_size_from_path,
            nb::arg("soc_descriptor_path"),
            release_gil(),
            "Get grid size from a YAML SoC descriptor file path");

    // Bind SocDescriptor class.
    nb::class_<SocDescriptor>(m, "SocDescriptor")
        .def(
            "__init__",
            [](SocDescriptor *soc_desc, TTDevice &tt_device) {
                new (soc_desc) SocDescriptor(tt_device.get_soc_descriptor());
            },
            nb::arg("tt_device"),
            release_gil(),
            "Create a SocDescriptor from a TTDevice")
        .def(
            "get_cores",
            &SocDescriptor::get_cores,
            nb::arg("core_type"),
            nb::arg("coord_system") = CoordSystem::NOC0,
            nb::arg("channel") = std::nullopt,
            release_gil(),
            "Get all cores of a specific type in the specified coordinate system")
        .def(
            "get_harvested_cores",
            &SocDescriptor::get_harvested_cores,
            nb::arg("core_type"),
            nb::arg("coord_system") = CoordSystem::NOC0,
            release_gil(),
            "Get all harvested cores of a specific type in the specified coordinate system")
        .def(
            "get_all_cores",
            &SocDescriptor::get_all_cores,
            nb::arg("coord_system") = CoordSystem::NOC0,
            release_gil(),
            "Get all cores in the specified coordinate system")
        .def(
            "get_all_harvested_cores",
            &SocDescriptor::get_all_harvested_cores,
            nb::arg("coord_system") = CoordSystem::NOC0,
            release_gil(),
            "Get all harvested cores in the specified coordinate system")
        .def(
            "serialize_to_file",
            [](const SocDescriptor &self, const std::string &dest_file) -> std::string {
                std::filesystem::path file_path = self.serialize_to_file(dest_file);
                return file_path.string();
            },
            nb::arg("dest_file") = "",
            release_gil(),
            "Serialize the soc descriptor to a YAML file")
        .def(
            "get_eth_cores_for_channels",
            &SocDescriptor::get_eth_cores_for_channels,
            nb::arg("eth_channels"),
            nb::arg("coord_system") = CoordSystem::NOC0,
            release_gil(),
            "Get ethernet cores for specified channels in the specified coordinate system")
        .def(
            "translate_coord_to",
            nb::overload_cast<const CoreCoord, const CoordSystem>(&SocDescriptor::translate_coord_to, nb::const_),
            nb::arg("core_coord"),
            nb::arg("coord_system"),
            release_gil(),
            "Translate a CoreCoord to the specified coordinate system")
        .def(
            "translate_coord_to",
            nb::overload_cast<const tt_xy_pair, const CoordSystem, const CoordSystem>(
                &SocDescriptor::translate_coord_to, nb::const_),
            nb::arg("core_location"),
            nb::arg("input_coord_system"),
            nb::arg("target_coord_system"),
            release_gil(),
            "Translate a tt_xy_pair from one coordinate system to another")
        .def(
            "translate_chip_coord_to_translated",
            &SocDescriptor::translate_chip_coord_to_translated,
            nb::arg("core"),
            release_gil(),
            "Translate a chip coordinate to translated coordinate system in tt_xy_pair")
        .def(
            "translate_chip_coord_to_translated_coord",
            &SocDescriptor::translate_chip_coord_to_translated_coord,
            nb::arg("core"),
            release_gil(),
            "Translate a chip coordinate to the correct pre-translation coordinates for device access")
        .def(
            "get_coord_at",
            &SocDescriptor::get_coord_at,
            nb::arg("core"),
            nb::arg("coord_system"),
            release_gil(),
            "Get a CoreCoord at the given tt_xy_pair location in the specified coordinate system");
}
