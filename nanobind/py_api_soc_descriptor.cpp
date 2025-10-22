/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// nanobind/py_api_soc_descriptor.cpp
// Exports tt::umd::SocDescriptor to Python using nanobind

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/soc_descriptor.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

void bind_soc_descriptor(nb::module_ &m) {
    nb::class_<SocDescriptor>(m, "SocDescriptor")
        .def(nb::init<>())
        .def(
            nb::init<const std::string &, const ChipInfo>(),
            nb::arg("device_descriptor_path"),
            nb::arg("chip_info") = ChipInfo{})
        .def(nb::init<const tt::ARCH, const ChipInfo>(), nb::arg("arch"), nb::arg("chip_info") = ChipInfo{})
        .def_static("get_arch_from_soc_descriptor_path", &SocDescriptor::get_arch_from_soc_descriptor_path)
        .def_static("get_grid_size_from_soc_descriptor_path", &SocDescriptor::get_grid_size_from_soc_descriptor_path)
        .def(
            "translate_coord_to",
            nb::overload_cast<const CoreCoord, const CoordSystem>(&SocDescriptor::translate_coord_to, nb::const_))
        .def(
            "translate_coord_to",
            nb::overload_cast<const tt_xy_pair, const CoordSystem, const CoordSystem>(
                &SocDescriptor::translate_coord_to, nb::const_))
        .def("translate_coords_to", &SocDescriptor::translate_coords_to)
        .def("translate_coords_to_xy_pair", &SocDescriptor::translate_coords_to_xy_pair)
        .def("get_coord_at", &SocDescriptor::get_coord_at, nb::arg("core"), nb::arg("coord_system"))
        .def("serialize", &SocDescriptor::serialize)
        .def("serialize_to_file", &SocDescriptor::serialize_to_file, nb::arg("dest_file") = "")
        .def_static("get_soc_descriptor_path", &SocDescriptor::get_soc_descriptor_path)
        .def("get_cores", &SocDescriptor::get_cores, nb::arg("core_type"), nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_harvested_cores",
            &SocDescriptor::get_harvested_cores,
            nb::arg("core_type"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def("get_all_cores", &SocDescriptor::get_all_cores, nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_all_harvested_cores",
            &SocDescriptor::get_all_harvested_cores,
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def("get_grid_size", &SocDescriptor::get_grid_size)
        .def("get_harvested_grid_size", &SocDescriptor::get_harvested_grid_size)
        .def("get_dram_cores", &SocDescriptor::get_dram_cores)
        .def("get_harvested_dram_cores", &SocDescriptor::get_harvested_dram_cores)
        .def("get_num_dram_channels", &SocDescriptor::get_num_dram_channels)
        .def("get_num_eth_channels", &SocDescriptor::get_num_eth_channels)
        .def("get_num_harvested_eth_channels", &SocDescriptor::get_num_harvested_eth_channels)
        .def(
            "get_dram_core_for_channel",
            &SocDescriptor::get_dram_core_for_channel,
            nb::arg("dram_chan"),
            nb::arg("subchannel"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_eth_core_for_channel",
            &SocDescriptor::get_eth_core_for_channel,
            nb::arg("eth_chan"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_eth_cores_for_channels",
            &SocDescriptor::get_eth_cores_for_channels,
            nb::arg("eth_channels"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_eth_xy_pairs_for_channels",
            &SocDescriptor::get_eth_xy_pairs_for_channels,
            nb::arg("eth_channels"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_eth_channel_for_core",
            &SocDescriptor::get_eth_channel_for_core,
            nb::arg("core_coord"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        .def(
            "get_dram_channel_for_core",
            &SocDescriptor::get_dram_channel_for_core,
            nb::arg("core_coord"),
            nb::arg("coord_system") = CoordSystem::NOC0)
        // Public member variables
        .def_rw("arch", &SocDescriptor::arch)
        .def_rw("grid_size", &SocDescriptor::grid_size)
        .def_rw("trisc_sizes", &SocDescriptor::trisc_sizes)
        .def_rw("device_descriptor_file_path", &SocDescriptor::device_descriptor_file_path)
        .def_rw("overlay_version", &SocDescriptor::overlay_version)
        .def_rw("unpacker_version", &SocDescriptor::unpacker_version)
        .def_rw("dst_size_alignment", &SocDescriptor::dst_size_alignment)
        .def_rw("packer_version", &SocDescriptor::packer_version)
        .def_rw("worker_l1_size", &SocDescriptor::worker_l1_size)
        .def_rw("eth_l1_size", &SocDescriptor::eth_l1_size)
        .def_rw("dram_bank_size", &SocDescriptor::dram_bank_size)
        .def_rw("noc_translation_enabled", &SocDescriptor::noc_translation_enabled)
        .def_rw("harvesting_masks", &SocDescriptor::harvesting_masks);
}
