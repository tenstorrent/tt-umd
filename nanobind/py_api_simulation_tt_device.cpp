// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/unique_ptr.h>

#include "tt-umd/soc_descriptor.hpp"
#include "tt-umd/tt_device/tt_device.hpp"
#include "tt-umd/types/core_coordinates.hpp"
#include "tt-umd/types/risc_type.hpp"
#include "tt_device/rtl_simulation_tt_device.hpp"
#include "tt_device/tt_sim_tt_device.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt::umd;

void bind_simulation_tt_devices(nb::module_ &m) {
    nb::class_<TTSimTTDevice, TTDevice>(m, "TTSimTTDevice")
        .def_static(
            "create",
            &TTSimTTDevice::create,
            nb::arg("simulator_directory"),
            nb::arg("num_host_mem_channels") = 0,
            release_gil(),
            "Creates a TTSimTTDevice for functional simulation communication.")
        .def(
            "assert_risc_reset",
            &TTSimTTDevice::assert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            nb::arg("noc_id") = NocId::DEFAULT_NOC,
            release_gil(),
            "Assert RISC reset for selected RISC cores on a given core.")
        .def(
            "deassert_risc_reset",
            &TTSimTTDevice::deassert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            nb::arg("staggered_start") = false,
            nb::arg("noc_id") = NocId::DEFAULT_NOC,
            release_gil(),
            "Deassert RISC reset for selected RISC cores on a given core.")
        .def(
            "get_soc_descriptor",
            &TTSimTTDevice::get_soc_descriptor,
            nb::rv_policy::reference_internal,
            "Get the SocDescriptor associated with this simulation device.")

        .def(
            "get_clock",
            &TTSimTTDevice::get_clock,
            release_gil(),
            "Get the current AICLK frequency in MHz, or 0 when the device reports none.")
        .def(
            "get_min_clock_freq", &TTSimTTDevice::get_min_clock_freq, release_gil(), "Get the minimum clock frequency.")
        .def_rw("bar0_base", &TTSimTTDevice::bar0_base, "Base address for BAR0.")
        .def_rw("bar4_base", &TTSimTTDevice::bar4_base, "Base address for BAR4.");

    nb::class_<RtlSimulationTTDevice, TTDevice>(m, "RtlSimulationTTDevice")
        .def_static(
            "create",
            &RtlSimulationTTDevice::create,
            nb::arg("simulator_directory"),
            nb::arg("num_host_mem_channels") = 0,
            release_gil(),
            "Creates an RtlSimulationTTDevice for RTL simulation communication.")
        .def(
            "assert_risc_reset",
            &RtlSimulationTTDevice::assert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            nb::arg("noc_id") = NocId::DEFAULT_NOC,
            release_gil(),
            "Assert RISC reset for selected RISC cores on a given core.")
        .def(
            "deassert_risc_reset",
            &RtlSimulationTTDevice::deassert_risc_reset,
            nb::arg("core"),
            nb::arg("selected_riscs"),
            nb::arg("staggered_start") = false,
            nb::arg("noc_id") = NocId::DEFAULT_NOC,
            release_gil(),
            "Deassert RISC reset for selected RISC cores on a given core.")
        .def(
            "get_soc_descriptor",
            &RtlSimulationTTDevice::get_soc_descriptor,
            nb::rv_policy::reference_internal,
            release_gil(),
            "Get the SocDescriptor associated with this RTL simulation device.");
}
