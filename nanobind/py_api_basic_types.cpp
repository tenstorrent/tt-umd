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
#include "umd/device/utils/semver.hpp"

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

    nb::class_<semver_t>(m, "semver_t")
        .def(nb::init<uint64_t, uint64_t, uint64_t>(), nb::arg("major"), nb::arg("minor"), nb::arg("patch"))
        .def(nb::init<const std::string &>(), nb::arg("version_str"))
        .def_rw("major", &semver_t::major)
        .def_rw("minor", &semver_t::minor)
        .def_rw("patch", &semver_t::patch)
        .def("__str__", &semver_t::to_string)
        .def(
            "__repr__",
            [](const semver_t &v) { return fmt::format("semver_t({}, {}, {})", v.major, v.minor, v.patch); })
        .def("__lt__", &semver_t::operator<)
        .def("__gt__", &semver_t::operator>)
        .def("__eq__", &semver_t::operator==)
        .def("__ne__", &semver_t::operator!=)
        .def("__le__", &semver_t::operator<=)
        .def("__ge__", &semver_t::operator>=)
        .def_static("compare_firmware_bundle", &semver_t::compare_firmware_bundle, nb::arg("v1"), nb::arg("v2"));
}
