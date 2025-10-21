/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <nanobind/nanobind.h>
#include <nanobind/stl/vector.h>

#include "umd/device/warm_reset.hpp"

namespace nb = nanobind;

using namespace tt::umd;

void bind_warm_reset(nb::module_ &m) {
    // WarmReset class binding
    nb::class_<WarmReset>(m, "WarmReset")
        .def_static(
            "warm_reset",
            &WarmReset::warm_reset,
            nb::arg("pci_device_ids"),
            nb::arg("reset_m3") = false,
            "Perform a warm reset of the device. reset_m3 flag sends specific ARC message to do a M3 board level "
            "reset.")
        .def_static(
            "ubb_warm_reset",
            &WarmReset::ubb_warm_reset,
            nb::arg("timeout_s") = 100,
            "Perform a UBB warm reset with specified timeout in seconds.");
}
