// SPDX-FileCopyrightText: © 2025 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <nanobind/nanobind.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "umd/device/warm_reset.hpp"
#include "umd/device/warm_reset_with_recovery.hpp"

namespace nb = nanobind;
// Releases Python's Global Interpreter Lock (GIL) for the duration of the C++ call,
// allowing other Python threads to run in parallel while this binding executes. Pass
// release_gil() as a call guard to nb::class_::def() on methods that don't touch the
// Python interpreter (e.g. blocking device I/O), so callers can drive UMD concurrently
// from multiple Python threads.
using release_gil = nb::call_guard<nb::gil_scoped_release>;

using namespace tt::umd;

void bind_warm_reset(nb::module_ &m) {
    // WarmReset class binding.
    nb::class_<WarmReset>(m, "WarmReset")
        .def_static(
            "warm_reset",
            &WarmReset::warm_reset,
            nb::arg("pci_device_ids") = std::vector<int>{},
            nb::arg("reset_m3") = false,
            nb::arg("secondary_bus_reset") = true,  // default to true for backward compatibility
            nb::arg("m3_delay_s") = 20.0,
            release_gil(),
            "Perform a warm reset of the device. reset_m3 flag sends specific ARC message to do a M3 board level "
            "reset. secondary_bus_reset flag performs a RESET_PCIE_LINK before issuing the ASIC reset. "
            "m3_delay_s is the post-reset wait time in seconds when reset_m3 is True (default 20s).")
        .def_static(
            "warm_reset_chip_id",
            &WarmReset::warm_reset_chip_id,
            nb::arg("chip_ids") = std::vector<int>{},
            nb::arg("reset_m3") = false,
            nb::arg("secondary_bus_reset") = true,
            nb::arg("m3_delay_s") = 20.0,
            release_gil(),
            "Perform a warm reset of the device using chip IDs. reset_m3 flag sends specific ARC message to do a M3 "
            "board level reset. secondary_bus_reset flag performs a RESET_PCIE_LINK before issuing the ASIC reset. "
            "m3_delay_s is the post-reset wait time in seconds when reset_m3 is True (default 20s).")
        .def_static(
            "warm_reset_pci_bdfs",
            &WarmReset::warm_reset_pci_bdfs,
            nb::arg("pci_bdfs") = std::vector<std::string>{},
            nb::arg("reset_m3") = false,
            nb::arg("secondary_bus_reset") = true,
            nb::arg("m3_delay_s") = 20.0,
            release_gil(),
            "Perform a warm reset of the device using PCI BDFs. reset_m3 flag sends specific ARC message to do a M3 "
            "board level reset. secondary_bus_reset flag performs a RESET_PCIE_LINK before issuing the ASIC reset. "
            "m3_delay_s is the post-reset wait time in seconds when reset_m3 is True (default 20s).")
        .def_static(
            "ubb_warm_reset",
            &WarmReset::ubb_warm_reset,
            nb::arg("timeout_s") = 100.0,
            release_gil(),
            "Perform a UBB warm reset with specified timeout in seconds.");

    // WarmResetWithRecovery class binding. Each method runs WarmReset followed by a
    // TopologyDiscovery::discover(); if discovery fails, another warm reset is performed
    // and the sequence is retried up to max_attempts times.
    nb::class_<WarmResetWithRecovery>(m, "WarmResetWithRecovery")
        .def_static(
            "warm_reset",
            &WarmResetWithRecovery::warm_reset,
            nb::arg("max_attempts") = 3,
            nb::arg("reset_m3") = false,
            nb::arg("secondary_bus_reset") = true,
            nb::arg("m3_delay_s") = 20.0,
            release_gil(),
            "Perform a warm reset on all enumerated devices and verify with topology discovery. "
            "Retries the (reset, discovery) sequence up to max_attempts times if discovery fails.")
        .def_static(
            "ubb_warm_reset",
            &WarmResetWithRecovery::ubb_warm_reset,
            nb::arg("max_attempts") = 3,
            nb::arg("timeout_s") = 100.0,
            release_gil(),
            "Perform a UBB warm reset and verify with topology discovery. Retries on discovery failure.");
}
