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
#include "umd/device/types/risc_type.hpp"
#include "umd/device/types/tensix_soft_reset_options.hpp"
#include "umd/device/types/xy_pair.hpp"
#include "umd/device/utils/semver.hpp"

namespace nb = nanobind;
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt;
using namespace tt::umd;

void bind_basic_types(nb::module_ &m) {
    nb::enum_<NocId>(m, "NocId")
        .value("DEFAULT_NOC", NocId::DEFAULT_NOC)
        .value("NOC0", NocId::NOC0)
        .value("NOC1", NocId::NOC1)
        .value("SYSTEM_NOC", NocId::SYSTEM_NOC)
        .def(
            "__int__", [](NocId noc_id) { return static_cast<int>(noc_id); }, release_gil());

    m.def("set_thread_noc_id", &tt::umd::set_thread_noc_id, nb::arg("noc_id"), release_gil());

    nb::class_<EthCoord>(m, "EthCoord")
        .def(nb::init<>(), release_gil())
        .def(
            nb::init<int, int, int, int, int>(),
            nb::arg("cluster_id"),
            nb::arg("x"),
            nb::arg("y"),
            nb::arg("rack"),
            nb::arg("shelf"),
            release_gil())
        .def_rw("cluster_id", &EthCoord::cluster_id)
        .def_rw("x", &EthCoord::x)
        .def_rw("y", &EthCoord::y)
        .def_rw("rack", &EthCoord::rack)
        .def_rw("shelf", &EthCoord::shelf);

    nb::class_<tt::xy_pair>(m, "tt_xy_pair")
        .def(nb::init<uint32_t, uint32_t>(), nb::arg("x"), nb::arg("y"), release_gil())
        .def_ro("x", &tt_xy_pair::x)
        .def_ro("y", &tt_xy_pair::y)
        .def(
            "__str__", [](const tt_xy_pair &pair) { return fmt::format("({}, {})", pair.x, pair.y); }, release_gil());

    nb::enum_<tt::ARCH>(m, "ARCH")
        .value("WORMHOLE_B0", tt::ARCH::WORMHOLE_B0)
        .value("BLACKHOLE", tt::ARCH::BLACKHOLE)
        .value("QUASAR", tt::ARCH::QUASAR)
        .value("Invalid", tt::ARCH::Invalid)
        .def("__str__", &tt::arch_to_str, release_gil())
        .def(
            "__int__", [](tt::ARCH tag) { return static_cast<int>(tag); }, release_gil())
        .def_static("from_str", &tt::arch_from_str, nb::arg("arch_str"), release_gil());

    nb::enum_<RiscType>(m, "RiscType")
        .value("NONE", RiscType::NONE)
        .value("ALL", RiscType::ALL)
        .value("ALL_TRISCS", RiscType::ALL_TRISCS)
        .value("ALL_DATA_MOVEMENT", RiscType::ALL_DATA_MOVEMENT)
        .value("BRISC", RiscType::BRISC)
        .value("TRISC0", RiscType::TRISC0)
        .value("TRISC1", RiscType::TRISC1)
        .value("TRISC2", RiscType::TRISC2)
        .value("NCRISC", RiscType::NCRISC)
        .value("ERISC0", RiscType::ERISC0)
        .value("ERISC1", RiscType::ERISC1)
        .value("ALL_TENSIX_TRISCS", RiscType::ALL_TENSIX_TRISCS)
        .value("ALL_TENSIX_DMS", RiscType::ALL_TENSIX_DMS)
        .value("ALL_TENSIX", RiscType::ALL_TENSIX)
        .value("NEO0_TRISC0", RiscType::NEO0_TRISC0)
        .value("NEO0_TRISC1", RiscType::NEO0_TRISC1)
        .value("NEO0_TRISC2", RiscType::NEO0_TRISC2)
        .value("NEO0_TRISC3", RiscType::NEO0_TRISC3)
        .value("NEO1_TRISC0", RiscType::NEO1_TRISC0)
        .value("NEO1_TRISC1", RiscType::NEO1_TRISC1)
        .value("NEO1_TRISC2", RiscType::NEO1_TRISC2)
        .value("NEO1_TRISC3", RiscType::NEO1_TRISC3)
        .value("NEO2_TRISC0", RiscType::NEO2_TRISC0)
        .value("NEO2_TRISC1", RiscType::NEO2_TRISC1)
        .value("NEO2_TRISC2", RiscType::NEO2_TRISC2)
        .value("NEO2_TRISC3", RiscType::NEO2_TRISC3)
        .value("NEO3_TRISC0", RiscType::NEO3_TRISC0)
        .value("NEO3_TRISC1", RiscType::NEO3_TRISC1)
        .value("NEO3_TRISC2", RiscType::NEO3_TRISC2)
        .value("NEO3_TRISC3", RiscType::NEO3_TRISC3)
        .value("DM0", RiscType::DM0)
        .value("DM1", RiscType::DM1)
        .value("DM2", RiscType::DM2)
        .value("DM3", RiscType::DM3)
        .value("DM4", RiscType::DM4)
        .value("DM5", RiscType::DM5)
        .value("DM6", RiscType::DM6)
        .value("DM7", RiscType::DM7)
        .value("ALL_NEO0_TRISCS", RiscType::ALL_NEO0_TRISCS)
        .value("ALL_NEO1_TRISCS", RiscType::ALL_NEO1_TRISCS)
        .value("ALL_NEO2_TRISCS", RiscType::ALL_NEO2_TRISCS)
        .value("ALL_NEO3_TRISCS", RiscType::ALL_NEO3_TRISCS)
        .value("ALL_NEO_TRISCS", RiscType::ALL_NEO_TRISCS)
        .value("ALL_NEO_DMS", RiscType::ALL_NEO_DMS)
        .value("ALL_NEO", RiscType::ALL_NEO)
        .def(
            "__int__", [](RiscType rt) { return static_cast<uint64_t>(rt); }, release_gil())
        .def(
            "__str__", [](RiscType rt) { return RiscTypeToString(rt); }, release_gil())
        .def(
            "__or__", [](RiscType lhs, RiscType rhs) { return lhs | rhs; }, release_gil())
        .def(
            "__and__", [](RiscType lhs, RiscType rhs) { return lhs & rhs; }, release_gil())
        .def(
            "__invert__", [](RiscType rt) { return invert_selected_options(rt); }, release_gil());

    nb::enum_<TensixSoftResetOptions>(m, "TensixSoftResetOptions")
        .value("NONE", TensixSoftResetOptions::NONE)
        .value("BRISC", TensixSoftResetOptions::BRISC)
        .value("TRISC0", TensixSoftResetOptions::TRISC0)
        .value("TRISC1", TensixSoftResetOptions::TRISC1)
        .value("TRISC2", TensixSoftResetOptions::TRISC2)
        .value("NCRISC", TensixSoftResetOptions::NCRISC)
        .value("STAGGERED_START", TensixSoftResetOptions::STAGGERED_START)
        .value("ALL_TRISC_SOFT_RESET", ALL_TRISC_SOFT_RESET)
        .value("ALL_TENSIX_SOFT_RESET", ALL_TENSIX_SOFT_RESET)
        .value("TENSIX_ASSERT_SOFT_RESET", TENSIX_ASSERT_SOFT_RESET)
        .value("TENSIX_DEASSERT_SOFT_RESET", TENSIX_DEASSERT_SOFT_RESET)
        .value("TENSIX_DEASSERT_SOFT_RESET_NO_STAGGER", TENSIX_DEASSERT_SOFT_RESET_NO_STAGGER)
        .def(
            "__int__", [](TensixSoftResetOptions opt) { return static_cast<uint32_t>(opt); }, release_gil())
        .def(
            "__str__", [](TensixSoftResetOptions opt) { return TensixSoftResetOptionsToString(opt); }, release_gil())
        .def(
            "__or__", [](TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) { return lhs | rhs; }, release_gil())
        .def(
            "__and__", [](TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) { return lhs & rhs; }, release_gil())
        .def(
            "__invert__", [](TensixSoftResetOptions opt) { return invert_selected_options(opt); }, release_gil());

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
        .value("QUASAR", tt::BoardType::QUASAR_BOARD)
        .value("UNKNOWN", tt::BoardType::UNKNOWN)
        .def("__str__", &tt::board_type_to_string, release_gil())
        .def(
            "__int__", [](tt::BoardType tag) { return static_cast<int>(tag); }, release_gil());

    nb::class_<SemVer>(m, "SemVer")
        .def(nb::init<>(), release_gil())
        .def(
            nb::init<uint64_t, uint64_t, uint64_t>(),
            nb::arg("major"),
            nb::arg("minor"),
            nb::arg("patch"),
            release_gil())
        .def(nb::init<const std::string &>(), nb::arg("version_str"), release_gil())
        .def_rw("major", &SemVer::major)
        .def_rw("minor", &SemVer::minor)
        .def_rw("patch", &SemVer::patch)
        .def("to_string", &SemVer::to_string, release_gil())
        .def("__str__", &SemVer::to_string, release_gil())
        .def("__lt__", &SemVer::operator<, release_gil())
        .def("__le__", &SemVer::operator<=, release_gil())
        .def("__gt__", &SemVer::operator>, release_gil())
        .def("__ge__", &SemVer::operator>=, release_gil())
        .def("__eq__", &SemVer::operator==, release_gil())
        .def("__ne__", &SemVer::operator!=, release_gil());
    // TODO: Remove after renaming in tt-exalens.
    m.attr("semver_t") = m.attr("SemVer");

    nb::class_<FirmwareBundleVersion>(m, "FirmwareBundleVersion")
        .def(nb::init<>(), release_gil())
        .def_static(
            "from_firmware_bundle_tag", &FirmwareBundleVersion::from_firmware_bundle_tag, nb::arg("tag"), release_gil())
        .def(
            nb::init<uint64_t, uint64_t, uint64_t>(),
            nb::arg("major"),
            nb::arg("minor"),
            nb::arg("patch"),
            release_gil())
        .def(nb::init<const std::string &>(), nb::arg("version_str"), release_gil())
        .def_rw("major", &SemVer::major)
        .def_rw("minor", &SemVer::minor)
        .def_rw("patch", &SemVer::patch)
        .def("to_string", &FirmwareBundleVersion::to_string, release_gil())
        .def("__str__", &FirmwareBundleVersion::to_string, release_gil())
        .def("__lt__", &FirmwareBundleVersion::operator<, release_gil())
        .def("__le__", &FirmwareBundleVersion::operator<=, release_gil())
        .def("__gt__", &FirmwareBundleVersion::operator>, release_gil())
        .def("__ge__", &FirmwareBundleVersion::operator>=, release_gil())
        .def("__eq__", &FirmwareBundleVersion::operator==, release_gil())
        .def("__ne__", &FirmwareBundleVersion::operator!=, release_gil());

    nb::class_<ChipInfo>(m, "ChipInfo")
        .def(nb::init<>(), release_gil())
        .def_rw("noc_translation_enabled", &ChipInfo::noc_translation_enabled)
        .def_rw("harvesting_masks", &ChipInfo::harvesting_masks)
        .def_rw("board_type", &ChipInfo::board_type)
        .def_rw("board_id", &ChipInfo::board_id)
        .def_rw("asic_location", &ChipInfo::asic_location);

    nb::class_<HarvestingMasks>(m, "HarvestingMasks")
        .def(nb::init<>(), release_gil())
        .def_rw("tensix_harvesting_mask", &HarvestingMasks::tensix_harvesting_mask)
        .def_rw("dram_harvesting_mask", &HarvestingMasks::dram_harvesting_mask)
        .def_rw("eth_harvesting_mask", &HarvestingMasks::eth_harvesting_mask)
        .def_rw("pcie_harvesting_mask", &HarvestingMasks::pcie_harvesting_mask)
        .def_rw("l2cpu_harvesting_mask", &HarvestingMasks::l2cpu_harvesting_mask);

    // Utility functions for BoardType.
    m.def(
        "board_type_to_string",
        &tt::board_type_to_string,
        nb::arg("board_type"),
        release_gil(),
        "Convert BoardType to string");
    m.def(
        "board_type_from_string",
        &tt::board_type_from_string,
        nb::arg("board_type_str"),
        release_gil(),
        "Convert string to BoardType");
}
