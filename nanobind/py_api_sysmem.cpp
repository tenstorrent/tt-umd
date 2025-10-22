/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
// nanobind/py_api_sysmem.cpp
// Exports tt::umd::SysmemBuffer and tt::umd::SysmemManager to Python using nanobind

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>

#include "umd/device/chip_helpers/sysmem_manager.hpp"

namespace nb = nanobind;

void bind_sysmem(nb::module_ &m) {
    nb::class_<tt::umd::SysmemBuffer>(m, "SysmemBuffer")
        .def(nb::init<tt::umd::TLBManager *, void *, size_t, bool>())
        .def("get_buffer_va", &tt::umd::SysmemBuffer::get_buffer_va)
        .def("get_buffer_size", &tt::umd::SysmemBuffer::get_buffer_size)
        .def("get_device_io_addr", &tt::umd::SysmemBuffer::get_device_io_addr)
        .def("get_noc_addr", &tt::umd::SysmemBuffer::get_noc_addr)
        .def("dma_write_to_device", &tt::umd::SysmemBuffer::dma_write_to_device)
        .def("dma_read_from_device", &tt::umd::SysmemBuffer::dma_read_from_device);

    nb::class_<tt::umd::SysmemManager>(m, "SysmemManager")
        .def(nb::init<tt::umd::TLBManager *, uint32_t>())
        .def("write_to_sysmem", &tt::umd::SysmemManager::write_to_sysmem)
        .def("read_from_sysmem", &tt::umd::SysmemManager::read_from_sysmem)
        .def("pin_or_map_sysmem_to_device", &tt::umd::SysmemManager::pin_or_map_sysmem_to_device)
        .def("unpin_or_unmap_sysmem", &tt::umd::SysmemManager::unpin_or_unmap_sysmem)
        .def("get_num_host_mem_channels", &tt::umd::SysmemManager::get_num_host_mem_channels)
        .def("get_hugepage_mapping", &tt::umd::SysmemManager::get_hugepage_mapping)
        .def("allocate_sysmem_buffer", &tt::umd::SysmemManager::allocate_sysmem_buffer)
        .def("map_sysmem_buffer", &tt::umd::SysmemManager::map_sysmem_buffer);
}
