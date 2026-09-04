// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include "umd/device/tt_device_model/tt_device_model.hpp"

#include "umd/device/utils/error.hpp"

namespace tt::umd {

void TTDeviceModel::read_from_arc_apb(
    [[maybe_unused]] void *mem_ptr,
    [[maybe_unused]] uint64_t arc_addr_offset,
    [[maybe_unused]] size_t size,
    [[maybe_unused]] NocId noc_id) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported for this device.");
}

void TTDeviceModel::write_to_arc_apb(
    [[maybe_unused]] const void *mem_ptr,
    [[maybe_unused]] uint64_t arc_addr_offset,
    [[maybe_unused]] size_t size,
    [[maybe_unused]] NocId noc_id) {
    UMD_THROW(error::RuntimeError, "ARC APB access is not supported for this device.");
}

void TTDeviceModel::configure_iatu_region(
    [[maybe_unused]] size_t region, [[maybe_unused]] uint64_t target, [[maybe_unused]] size_t region_size) {
    UMD_THROW(error::RuntimeError, "configure_iatu_region is not implemented for this device.");
}

}  // namespace tt::umd
