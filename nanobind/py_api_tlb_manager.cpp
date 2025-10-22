/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// nanobind/py_api_tlb_manager.cpp
// Exports tt::umd::TLBManager to Python using nanobind

#include <nanobind/nanobind.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>

#include "umd/device/chip_helpers/tlb_manager.hpp"
#include "umd/device/tt_device/tt_device.hpp"
#include "umd/device/types/xy_pair.hpp"

namespace nb = nanobind;

using namespace tt;
using namespace tt::umd;

// Define a custom deleter for TLBManager to prevent copy issues
struct TLBManagerDeleter {
    void operator()(TLBManager* ptr) const { delete ptr; }
};

void bind_tlb_manager(nb::module_& m) {
    nb::class_<TLBManager>(m, "TLBManager")
        .def("__init__", [](TLBManager* self, TTDevice* device) { new (self) TLBManager(device); })
        .def("configure_tlb", &TLBManager::configure_tlb)
        .def("configure_tlb_kmd", &TLBManager::configure_tlb_kmd)
        .def("set_dynamic_tlb_config", &TLBManager::set_dynamic_tlb_config)
        .def("set_dynamic_tlb_config_ordering", &TLBManager::set_dynamic_tlb_config_ordering)
        .def("address_in_tlb_space", &TLBManager::address_in_tlb_space)
        .def("is_tlb_mapped", nb::overload_cast<tt_xy_pair>(&TLBManager::is_tlb_mapped))
        .def("is_tlb_mapped_addr", nb::overload_cast<tt_xy_pair, uint64_t, uint32_t>(&TLBManager::is_tlb_mapped))
        .def("get_tlb_configuration", &TLBManager::get_tlb_configuration)
        .def("get_tt_device", &TLBManager::get_tt_device)
        .def("get_tlb_window", &TLBManager::get_tlb_window, nb::rv_policy::reference_internal);
}

// Prevent nanobind from generating copy constructor by specializing wrap_copy
namespace nanobind::detail {
template <>
void wrap_copy<TLBManager>(void* dst, const void* src) {
    (void)dst;
    (void)src;  // Suppress unused parameter warnings
    nb::raise("TLBManager is not copyable!");
}
}  // namespace nanobind::detail
