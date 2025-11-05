/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

void bind_basic_types(nb::module_ &m) {
    nb::class_<EthCoord>(m, "EthCoord")
        .def(nb::init<>())
        .def(
            nb::init<int, int, int, int, int>(),
            nb::arg("cluster_id"),
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("rack"),
            nb::arg("shelf"))
        .def_rw("cluster_id", &EthCoord::cluster_id)
        .def_rw("x", &EthCoord::x)
        .def_rw("y", &EthCoord::y)
        .def_rw("rack", &EthCoord::rack)
        .def_rw("shelf", &EthCoord::shelf);

    nb::class_<tt::xy_pair>(m, "tt_xy_pair")
        .def(nb::init<uint32_t, uint32_t>(), nb::arg("x"), nb::arg("y"))
        .def_ro("x", &tt_xy_pair::x)
        .def_ro("y", &tt_xy_pair::y)
        .def("__str__", [](const tt_xy_pair &pair) { return fmt::format("({}, {})", pair.x, pair.y); });

    nb::enum_<tt::ARCH>(m, "ARCH")
        .value("WORMHOLE_B0", tt::ARCH::WORMHOLE_B0)
        .value("BLACKHOLE", tt::ARCH::BLACKHOLE)
        .value("QUASAR", tt::ARCH::QUASAR)
        .value("Invalid", tt::ARCH::Invalid)
        .def("__str__", &tt::arch_to_str)
        .def("__int__", [](tt::ARCH tag) { return static_cast<int>(tag); })
        .def_static("from_str", &tt::arch_from_str, nb::arg("arch_str"));

    nb::enum_<tt::BoardType>(m, "BoardType")
        .value("E75", tt::BoardType::E75)
        .value("E150", tt::BoardType::E150)
        .value("E300", tt::BoardType::E300)
        .value("N150", tt::BoardType::N150)
        .value("N300", tt::BoardType::N300)
        .value("P100", tt::BoardType::P100)
        .value("P150", tt::BoardType::P150)
        .value("P300", tt::BoardType::P300)
        .value("GALAXY", tt::BoardType::GALAXY)
        .value("UBB", tt::BoardType::UBB)
        .value("UBB_WORMHOLE", tt::BoardType::UBB_WORMHOLE)
        .value("UBB_BLACKHOLE", tt::BoardType::UBB_BLACKHOLE)
        .value("QUASAR", tt::BoardType::QUASAR)
        .value("UNKNOWN", tt::BoardType::UNKNOWN)
        .def("__str__", &tt::board_type_to_string)
        .def("__int__", [](tt::BoardType tag) { return static_cast<int>(tag); });
}
