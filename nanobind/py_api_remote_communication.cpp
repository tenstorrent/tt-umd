/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// nanobind/py_api_remote_communication.cpp
// Exports tt::umd::RemoteCommunication::create_remote_communication to Python using nanobind

#include <nanobind/nanobind.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_set.h>
#include <nanobind/stl/vector.h>

#include "umd/device/chip_helpers/sysmem_manager.hpp"
#include "umd/device/tt_device/remote_communication.hpp"

namespace nb = nanobind;

void bind_remote_communication(nb::module_& m) {
    nb::class_<tt::umd::RemoteCommunication>(m, "RemoteCommunication")
        .def_static(
            "create_remote_communication",
            [](tt::umd::TTDevice* local_tt_device, tt::EthCoord target_chip, tt::umd::SysmemManager* sysmem_manager) {
                return tt::umd::RemoteCommunication::create_remote_communication(
                    local_tt_device, target_chip, sysmem_manager);
            },
            nb::arg("local_tt_device"),
            nb::arg("target_chip"),
            nb::arg("sysmem_manager") = nullptr)
        .def(
            "set_remote_transfer_ethernet_cores",
            &tt::umd::RemoteCommunication::set_remote_transfer_ethernet_cores,
            nb::arg("cores"));
}
