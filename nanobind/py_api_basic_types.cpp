// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>

#include "umd/device/types/arch.hpp"
#include "umd/device/types/cluster_descriptor_types.hpp"
#include "umd/device/types/noc_id.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

void bind_basic_types(nb::module_ &m) {
    nb::enum_<NocId>(m, "NocId")
        .value("DEFAULT_NOC", NocId::DEFAULT_NOC)
        .value("NOC0", NocId::NOC0)
        .value("NOC1", NocId::NOC1)
        .value("SYSTEM_NOC", NocId::SYSTEM_NOC)
        .def("__int__", [](NocId noc_id) { return static_cast<int>(noc_id); });

    m.def("set_thread_noc_id", &tt::umd::set_thread_noc_id, nb::arg("noc_id"));

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

    nb::class_<SemVer>(m, "SemVer")
        .def(nb::init<>())
        .def(nb::init<uint64_t, uint64_t, uint64_t>(), nb::arg("major"), nb::arg("minor"), nb::arg("patch"))
        .def(nb::init<const std::string &>(), nb::arg("version_str"))
        .def_rw("major", &SemVer::major)
        .def_rw("minor", &SemVer::minor)
        .def_rw("patch", &SemVer::patch)
        .def("to_string", &SemVer::to_string)
        .def("__str__", &SemVer::to_string)
        .def("__lt__", &SemVer::operator<)
        .def("__le__", &SemVer::operator<=)
        .def("__gt__", &SemVer::operator>)
        .def("__ge__", &SemVer::operator>=)
        .def("__eq__", &SemVer::operator==)
        .def("__ne__", &SemVer::operator!=);
    // TODO: Remove after renaming in tt-exalens.
    m.attr("semver_t") = m.attr("SemVer");

    nb::class_<FirmwareBundleVersion>(m, "FirmwareBundleVersion")
        .def(nb::init<>())
        .def_static("from_firmware_bundle_tag", &FirmwareBundleVersion::from_firmware_bundle_tag, nb::arg("tag"))
        .def("to_string", &FirmwareBundleVersion::to_string)
        .def("__str__", &FirmwareBundleVersion::to_string)
        .def("__lt__", &FirmwareBundleVersion::operator<)
        .def("__le__", &FirmwareBundleVersion::operator<=)
        .def("__gt__", &FirmwareBundleVersion::operator>)
        .def("__ge__", &FirmwareBundleVersion::operator>=)
        .def("__eq__", &FirmwareBundleVersion::operator==)
        .def("__ne__", &FirmwareBundleVersion::operator!=);

    nb::class_<ChipInfo>(m, "ChipInfo")
        .def(nb::init<>())
        .def_rw("noc_translation_enabled", &ChipInfo::noc_translation_enabled)
        .def_rw("harvesting_masks", &ChipInfo::harvesting_masks)
        .def_rw("board_type", &ChipInfo::board_type)
        .def_rw("board_id", &ChipInfo::board_id)
        .def_rw("asic_location", &ChipInfo::asic_location);

    nb::class_<HarvestingMasks>(m, "HarvestingMasks")
        .def(nb::init<>())
        .def_rw("tensix_harvesting_mask", &HarvestingMasks::tensix_harvesting_mask)
        .def_rw("dram_harvesting_mask", &HarvestingMasks::dram_harvesting_mask)
        .def_rw("eth_harvesting_mask", &HarvestingMasks::eth_harvesting_mask)
        .def_rw("pcie_harvesting_mask", &HarvestingMasks::pcie_harvesting_mask)
        .def_rw("l2cpu_harvesting_mask", &HarvestingMasks::l2cpu_harvesting_mask);

    // Utility functions for BoardType.
    m.def("board_type_to_string", &tt::board_type_to_string, nb::arg("board_type"), "Convert BoardType to string");
    m.def(
        "board_type_from_string",
        &tt::board_type_from_string,
        nb::arg("board_type_str"),
        "Convert string to BoardType");
}
