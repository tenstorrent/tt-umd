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
#include "umd/device/types/core_coordinates.hpp"
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

    nb::class_<ChipInfo>(m, "ChipInfo")
        .def(nb::init<>())
        .def_rw("noc_translation_enabled", &ChipInfo::noc_translation_enabled)
        .def_rw("harvesting_masks", &ChipInfo::harvesting_masks)
        .def_rw("board_type", &ChipInfo::board_type)
        .def_rw("board_id", &ChipInfo::board_id)
        .def_rw("asic_location", &ChipInfo::asic_location);

    nb::class_<tt::xy_pair>(m, "tt_xy_pair")
        .def(nb::init<uint32_t, uint32_t>(), nb::arg("x"), nb::arg("y"))
        .def_ro("x", &tt_xy_pair::x)
        .def_ro("y", &tt_xy_pair::y)
        .def("__str__", [](const tt_xy_pair &pair) { return fmt::format("({}, {})", pair.x, pair.y); });

    nb::class_<tt::umd::CoreCoord, tt::xy_pair>(m, "CoreCoord")
        .def(nb::init<>())
        .def(
            nb::init<size_t, size_t, tt::CoreType, tt::CoordSystem>(),
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("core_type"),
            nb::arg("coord_system"))
        .def(
            nb::init<tt_xy_pair, tt::CoreType, tt::CoordSystem>(),
            nb::arg("core"),
            nb::arg("core_type"),
            nb::arg("coord_system"))
        .def_rw("core_type", &tt::umd::CoreCoord::core_type)
        .def_rw("coord_system", &tt::umd::CoreCoord::coord_system)
        .def("__str__", [](const tt::umd::CoreCoord &coord) { return coord.str(); })
        .def("__eq__", &tt::umd::CoreCoord::operator==)
        .def("__lt__", &tt::umd::CoreCoord::operator<);

    nb::enum_<tt::ARCH>(m, "ARCH")
        .value("WORMHOLE_B0", tt::ARCH::WORMHOLE_B0)
        .value("BLACKHOLE", tt::ARCH::BLACKHOLE)
        .value("QUASAR", tt::ARCH::QUASAR)
        .value("Invalid", tt::ARCH::Invalid)
        .def("__str__", &tt::arch_to_str)
        .def("__int__", [](tt::ARCH tag) { return static_cast<int>(tag); })
        .def_static("from_str", &tt::arch_from_str, nb::arg("arch_str"));

    nb::enum_<tt::CoreType>(m, "CoreType")
        .value("ARC", tt::CoreType::ARC)
        .value("DRAM", tt::CoreType::DRAM)
        .value("ACTIVE_ETH", tt::CoreType::ACTIVE_ETH)
        .value("IDLE_ETH", tt::CoreType::IDLE_ETH)
        .value("PCIE", tt::CoreType::PCIE)
        .value("TENSIX", tt::CoreType::TENSIX)
        .value("ROUTER_ONLY", tt::CoreType::ROUTER_ONLY)
        .value("SECURITY", tt::CoreType::SECURITY)
        .value("L2CPU", tt::CoreType::L2CPU)
        .value("HARVESTED", tt::CoreType::HARVESTED)
        .value("ETH", tt::CoreType::ETH)
        .value("WORKER", tt::CoreType::WORKER)
        .value("COUNT", tt::CoreType::COUNT)
        .def("__str__", [](tt::CoreType tag) { return tt::to_str(tag); })
        .def("__int__", [](tt::CoreType tag) { return static_cast<int>(tag); });

    nb::enum_<tt::CoordSystem>(m, "CoordSystem")
        .value("LOGICAL", tt::CoordSystem::LOGICAL)
        .value("TRANSLATED", tt::CoordSystem::TRANSLATED)
        .value("NOC0", tt::CoordSystem::NOC0)
        .value("NOC1", tt::CoordSystem::NOC1)
        .def("__str__", [](tt::CoordSystem tag) { return tt::to_str(tag); })
        .def("__int__", [](tt::CoordSystem tag) { return static_cast<int>(tag); });
}
