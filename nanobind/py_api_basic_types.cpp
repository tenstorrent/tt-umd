/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include "umd/device/types/arch.h"
#include "umd/device/types/cluster_descriptor_types.h"
#include "umd/device/types/xy_pair.h"

namespace nb = nanobind;

using namespace tt::umd;

void bind_basic_types(nb::module_ &m) {
    // Expose the eth_coord_t struct
    nb::class_<eth_coord_t>(m, "EthCoord")
        .def(nb::init<>())
        .def(
            nb::init<int, int, int, int, int>(),
            nb::arg("cluster_id"),
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("rack"),
            nb::arg("shelf"))
        .def_rw("cluster_id", &eth_coord_t::cluster_id)
        .def_rw("x", &eth_coord_t::x)
        .def_rw("y", &eth_coord_t::y)
        .def_rw("rack", &eth_coord_t::rack)
        .def_rw("shelf", &eth_coord_t::shelf);

    // Expose the tt_xy_pair
    nb::class_<tt::umd::xy_pair>(m, "tt_xy_pair")
        .def(nb::init<uint32_t, uint32_t>(), nb::arg("x"), nb::arg("y"))
        .def_ro("x", &tt_xy_pair::x)
        .def_ro("y", &tt_xy_pair::y)
        .def("__str__", [](const tt_xy_pair &pair) { return fmt::format("({}, {})", pair.x, pair.y); });

    // Expose tt::ARCH enum
    nb::enum_<tt::ARCH>(m, "ARCH")
        .value("WORMHOLE_B0", tt::ARCH::WORMHOLE_B0)
        .value("BLACKHOLE", tt::ARCH::BLACKHOLE)
        .value("QUASAR", tt::ARCH::QUASAR)
        .value("Invalid", tt::ARCH::Invalid)
        .def("__str__", &tt::arch_to_str)
        .def("__int__", [](tt::ARCH tag) { return static_cast<int>(tag); })
        .def_static("from_str", &tt::arch_from_str, nb::arg("arch_str"));
}
